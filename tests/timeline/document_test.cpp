#include "rf/timeline/document.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using rf::Errc;
using rf::media::Rational;
using rf::timeline::Clip;
using rf::timeline::ClipId;
using rf::timeline::Document;
using rf::timeline::Track;
using rf::timeline::TrackId;
using rf::timeline::TrackKind;

Document make_document() {
    auto document = Document::create(Rational{1, 90000});
    EXPECT_TRUE(document.has_value()) << (document.has_error() ? document.error().to_string() : "");
    return std::move(document).value();
}

TEST(Document, RejectsAZeroTimeBase) {
    const auto document = Document::create(Rational{0, 1});
    ASSERT_TRUE(document.has_error());
    EXPECT_EQ(document.error().code(), Errc::invalid_argument);
}

TEST(Document, StartsEmptyWithIdsUnspent) {
    const Document document = make_document();
    EXPECT_TRUE(document.tracks().empty());
    EXPECT_EQ(document.clip_count(), 0u);
    EXPECT_EQ(document.next_id(), 1u) << "id 0 is the null id and must never be issued";
}

TEST(Document, IssuesDistinctIds) {
    Document document = make_document();
    const auto a = document.add_track(TrackKind::video, "V1");
    const auto b = document.add_track(TrackKind::audio, "A1");
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_NE(a.value(), b.value());
    EXPECT_TRUE(a.value().is_valid());
}

TEST(Document, NullIdIsNeverIssuedAndNeverValid) {
    EXPECT_FALSE(TrackId{}.is_valid());
    EXPECT_FALSE(ClipId{}.is_valid());
}

// --- clips -------------------------------------------------------------------

TEST(Document, AddsAClipToATrack) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();

    const auto clip = document.add_clip(track, "a.mp4", 100, 0, 9000);
    ASSERT_TRUE(clip.has_value()) << clip.error().to_string();

    const Clip* stored = document.find_clip(clip.value());
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->source, "a.mp4");
    EXPECT_EQ(stored->source_in, 100);
    EXPECT_EQ(stored->start, 0);
    EXPECT_EQ(stored->duration, 9000);
    EXPECT_TRUE(stored->enabled);
}

TEST(Document, RejectsANonPositiveDuration) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    EXPECT_TRUE(document.add_clip(track, "a.mp4", 0, 0, 0).has_error());
    EXPECT_TRUE(document.add_clip(track, "a.mp4", 0, 0, -1).has_error());
}

TEST(Document, RejectsNegativePositions) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    EXPECT_TRUE(document.add_clip(track, "a.mp4", 0, -1, 100).has_error());
    EXPECT_TRUE(document.add_clip(track, "a.mp4", -1, 0, 100).has_error());
}

TEST(Document, RejectsAClipOnAMissingTrack) {
    Document document = make_document();
    const auto clip = document.add_clip(TrackId{999}, "a.mp4", 0, 0, 100);
    ASSERT_TRUE(clip.has_error());
    EXPECT_EQ(clip.error().code(), Errc::not_found);
}

TEST(Document, RefusesOverlappingClips) {
    // Two clips occupying the same instant on one track have no defined render
    // result, so the model refuses rather than letting the renderer decide.
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    ASSERT_TRUE(document.add_clip(track, "a.mp4", 0, 100, 100).has_value());

    EXPECT_TRUE(document.add_clip(track, "b.mp4", 0, 150, 100).has_error()) << "overlaps the tail";
    EXPECT_TRUE(document.add_clip(track, "b.mp4", 0, 50, 100).has_error()) << "overlaps the head";
    EXPECT_TRUE(document.add_clip(track, "b.mp4", 0, 120, 10).has_error()) << "wholly inside";
    EXPECT_TRUE(document.add_clip(track, "b.mp4", 0, 50, 200).has_error()) << "wholly contains";
}

TEST(Document, AllowsButtJoinedClips) {
    // Clips that touch end-to-start are the normal case in an edit, not an
    // overlap. Getting this boundary wrong makes ordinary cutting impossible.
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    ASSERT_TRUE(document.add_clip(track, "a.mp4", 0, 100, 100).has_value());
    EXPECT_TRUE(document.add_clip(track, "b.mp4", 0, 200, 100).has_value())
        << "a clip starting exactly where the previous ends must be allowed";
    EXPECT_TRUE(document.add_clip(track, "c.mp4", 0, 0, 100).has_value())
        << "a clip ending exactly where the next begins must be allowed";
}

TEST(Document, AllowsOverlapAcrossDifferentTracks) {
    Document document = make_document();
    const TrackId video = document.add_track(TrackKind::video, "V1").value();
    const TrackId audio = document.add_track(TrackKind::audio, "A1").value();
    ASSERT_TRUE(document.add_clip(video, "a.mp4", 0, 0, 100).has_value());
    EXPECT_TRUE(document.add_clip(audio, "a.mp4", 0, 0, 100).has_value())
        << "tracks are independent lanes";
}

TEST(Document, KeepsClipsOrderedByStart) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    ASSERT_TRUE(document.add_clip(track, "c.mp4", 0, 200, 50).has_value());
    ASSERT_TRUE(document.add_clip(track, "a.mp4", 0, 0, 50).has_value());
    ASSERT_TRUE(document.add_clip(track, "b.mp4", 0, 100, 50).has_value());

    const std::vector<Clip>& clips = document.find_track(track)->clips;
    ASSERT_EQ(clips.size(), 3u);
    EXPECT_EQ(clips[0].start, 0);
    EXPECT_EQ(clips[1].start, 100);
    EXPECT_EQ(clips[2].start, 200);
}

// --- removal and restoration -------------------------------------------------

TEST(Document, RemoveClipReturnsEverythingNeededToRestoreIt) {
    // This is the contract undo depends on: an inverse that cannot reproduce
    // the clip exactly is a silent data-loss bug.
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    const ClipId id = document.add_clip(track, "a.mp4", 42, 300, 150).value();
    ASSERT_TRUE(document.set_clip_enabled(id, false).has_value());

    const Clip before = *document.find_clip(id);
    auto removed = document.remove_clip(id);
    ASSERT_TRUE(removed.has_value()) << removed.error().to_string();
    EXPECT_EQ(removed.value(), before);
    EXPECT_EQ(document.find_clip(id), nullptr);

    ASSERT_TRUE(document.insert_clip(track, removed.value()).has_value());
    EXPECT_EQ(*document.find_clip(id), before);
}

TEST(Document, RemoveTrackReturnsItsClipsToo) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    ASSERT_TRUE(document.add_clip(track, "a.mp4", 0, 0, 100).has_value());
    ASSERT_TRUE(document.add_clip(track, "b.mp4", 0, 100, 100).has_value());

    auto removed = document.remove_track(track);
    ASSERT_TRUE(removed.has_value()) << removed.error().to_string();
    EXPECT_EQ(removed.value().clips.size(), 2u);
    EXPECT_EQ(document.clip_count(), 0u);
}

TEST(Document, InsertTrackAtRestoresPosition) {
    Document document = make_document();
    const TrackId first = document.add_track(TrackKind::video, "V1").value();
    const TrackId second = document.add_track(TrackKind::video, "V2").value();
    const TrackId third = document.add_track(TrackKind::video, "V3").value();

    auto removed = document.remove_track(second);
    ASSERT_TRUE(removed.has_value());
    ASSERT_TRUE(document
                    .insert_track_at(1, removed.value().id, removed.value().kind,
                                     removed.value().name, removed.value().muted,
                                     removed.value().locked, removed.value().clips)
                    .has_value());

    ASSERT_EQ(document.tracks().size(), 3u);
    EXPECT_EQ(document.tracks()[0].id, first);
    EXPECT_EQ(document.tracks()[1].id, second) << "restored track landed in the wrong place";
    EXPECT_EQ(document.tracks()[2].id, third);
}

TEST(Document, RefusesToInsertADuplicateId) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    const ClipId id = document.add_clip(track, "a.mp4", 0, 0, 100).value();

    Clip duplicate = *document.find_clip(id);
    duplicate.start = 500;
    const auto inserted = document.insert_clip(track, duplicate);
    ASSERT_TRUE(inserted.has_error());
    EXPECT_EQ(inserted.error().code(), Errc::already_exists);
}

TEST(Document, RemovingSomethingAbsentIsAnError) {
    Document document = make_document();
    EXPECT_EQ(document.remove_clip(ClipId{7}).error().code(), Errc::not_found);
    EXPECT_EQ(document.remove_track(TrackId{7}).error().code(), Errc::not_found);
}

// --- moving and trimming -----------------------------------------------------

TEST(Document, MovesAClipWithinATrack) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    const ClipId id = document.add_clip(track, "a.mp4", 0, 0, 100).value();

    ASSERT_TRUE(document.move_clip(id, track, 500).has_value());
    EXPECT_EQ(document.find_clip(id)->start, 500);
    EXPECT_EQ(document.find_clip(id)->duration, 100) << "moving must not resize";
}

TEST(Document, MovesAClipBetweenTracks) {
    Document document = make_document();
    const TrackId from = document.add_track(TrackKind::video, "V1").value();
    const TrackId to = document.add_track(TrackKind::video, "V2").value();
    const ClipId id = document.add_clip(from, "a.mp4", 7, 0, 100).value();

    ASSERT_TRUE(document.move_clip(id, to, 200).has_value());
    EXPECT_EQ(document.track_of_clip(id)->id, to);
    EXPECT_EQ(document.find_clip(id)->source_in, 7) << "moving must not alter the source offset";
    EXPECT_TRUE(document.find_track(from)->clips.empty());
}

TEST(Document, RefusesAMoveThatWouldOverlap) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    const ClipId first = document.add_clip(track, "a.mp4", 0, 0, 100).value();
    ASSERT_TRUE(document.add_clip(track, "b.mp4", 0, 200, 100).has_value());

    const auto moved = document.move_clip(first, track, 250);
    ASSERT_TRUE(moved.has_error());
    EXPECT_EQ(document.find_clip(first)->start, 0) << "a refused move must change nothing";
}

TEST(Document, TrimsAClip) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    const ClipId id = document.add_clip(track, "a.mp4", 0, 0, 100).value();

    ASSERT_TRUE(document.set_clip_bounds(id, 30, 30, 70).has_value());
    const Clip* clip = document.find_clip(id);
    EXPECT_EQ(clip->source_in, 30);
    EXPECT_EQ(clip->start, 30);
    EXPECT_EQ(clip->duration, 70);
}

TEST(Document, RefusesATrimThatWouldOverlapANeighbour) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    const ClipId first = document.add_clip(track, "a.mp4", 0, 0, 100).value();
    ASSERT_TRUE(document.add_clip(track, "b.mp4", 0, 100, 100).has_value());

    const auto trimmed = document.set_clip_bounds(first, 0, 0, 150);
    ASSERT_TRUE(trimmed.has_error());
    EXPECT_EQ(document.find_clip(first)->duration, 100) << "a refused trim must change nothing";
}

TEST(Document, TogglesFlags) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    const ClipId clip = document.add_clip(track, "a.mp4", 0, 0, 100).value();

    ASSERT_TRUE(document.set_track_muted(track, true).has_value());
    ASSERT_TRUE(document.set_track_locked(track, true).has_value());
    ASSERT_TRUE(document.set_clip_enabled(clip, false).has_value());

    EXPECT_TRUE(document.find_track(track)->muted);
    EXPECT_TRUE(document.find_track(track)->locked);
    EXPECT_FALSE(document.find_clip(clip)->enabled);
}

TEST(Document, EqualityComparesEveryField) {
    // The fuzz relies on this alongside byte comparison: if the serialiser ever
    // omits a field, structural equality still catches a bad inverse.
    Document a = make_document();
    Document b = make_document();
    EXPECT_EQ(a, b);

    const TrackId track = a.add_track(TrackKind::video, "V1").value();
    EXPECT_NE(a, b);

    ASSERT_TRUE(b.add_track(TrackKind::video, "V1").has_value());
    EXPECT_EQ(a, b);

    ASSERT_TRUE(a.set_track_muted(track, true).has_value());
    EXPECT_NE(a, b) << "a flag difference must not compare equal";
}

}  // namespace
