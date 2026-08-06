#include "rf/timeline/document.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

using rf::Errc;
using rf::media::Rational;
using rf::timeline::Clip;
using rf::timeline::ClipId;
using rf::timeline::Document;
using rf::timeline::LinkId;
using rf::timeline::Track;
using rf::timeline::TrackId;
using rf::timeline::TrackKind;
using rf::timeline::Ticks;

/// Long enough that the source is never the binding constraint. Tests that are
/// about the media limit state their own source length.
constexpr Ticks kSourceTicks = 1'000'000;

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

    const auto clip = document.add_clip(track, "a.mp4", 100, 0, 9000, kSourceTicks);
    ASSERT_TRUE(clip.has_value()) << clip.error().to_string();

    const Clip* stored = document.find_clip(clip.value());
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->source, "a.mp4");
    EXPECT_EQ(stored->source_in, 100);
    EXPECT_EQ(stored->source_duration, kSourceTicks);
    EXPECT_EQ(stored->start, 0);
    EXPECT_EQ(stored->duration, 9000);
    EXPECT_TRUE(stored->enabled);
}

// --- the media limit (ADR 009) -----------------------------------------------
//
// These are the checks that make a trim limit computable at all. Without them
// the document can hold a clip whose frames do not exist, and the failure
// surfaces during playback as a missing picture rather than as a refused edit.

TEST(Document, RefusesAClipThatReachesPastItsSource) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();

    EXPECT_TRUE(document.add_clip(track, "a.mp4", 0, 0, 101, 100).has_error())
        << "one tick past the end is still past the end";
    EXPECT_TRUE(document.add_clip(track, "a.mp4", 50, 0, 51, 100).has_error())
        << "the offset counts against the available media";

    EXPECT_TRUE(document.add_clip(track, "a.mp4", 0, 0, 100, 100).has_value())
        << "consuming the source exactly must be allowed";
}

TEST(Document, RefusesAClipWhoseEndIsNotRepresentable) {
    // Not a case a user reaches -- a tick is 1/90000 s, so this is millions of
    // years out. It is enforced because the trim ranges are computed with plain
    // signed arithmetic, and that is only sound while every clip's end fits.
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    constexpr Ticks kMax = std::numeric_limits<Ticks>::max();

    EXPECT_TRUE(document.add_clip(track, "a.mp4", 0, kMax - 99, 100, kMax).has_error());
    EXPECT_TRUE(document.add_clip(track, "a.mp4", 0, kMax - 100, 100, kMax).has_value())
        << "ending exactly on the last representable tick is legal";
}

TEST(Document, RefusesANegativeSourceDuration) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    EXPECT_TRUE(document.add_clip(track, "a.mp4", 0, 0, 100, -1).has_error());
}

TEST(Document, RefusesATrimPastTheEndOfTheSource) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    const ClipId id = document.add_clip(track, "a.mp4", 10, 0, 80, 100).value();

    EXPECT_TRUE(document.set_clip_bounds(id, 10, 0, 91).has_error()) << "runs off the tail";
    EXPECT_TRUE(document.set_clip_bounds(id, 30, 0, 71).has_error()) << "offset plus length";
    EXPECT_TRUE(document.set_clip_bounds(id, 10, 0, 90).has_value()) << "exactly to the end";
    EXPECT_EQ(document.find_clip(id)->duration, 80 + 10);
}

TEST(Document, RefusesToInsertAClipThatReachesPastItsSource) {
    // insert_clip is undo's route back into the document, so it has to enforce
    // the same invariant as add_clip -- otherwise a restore could reintroduce a
    // clip the model would refuse to create.
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    const ClipId id = document.add_clip(track, "a.mp4", 0, 0, 100, 100).value();

    Clip removed = document.remove_clip(id).value();
    removed.duration = 101;
    EXPECT_TRUE(document.insert_clip(track, removed).has_error());
}

// --- whole-track replacement -------------------------------------------------

TEST(Document, ReplaceTrackClipsRewritesEveryClipAtOnce) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    const ClipId a = document.add_clip(track, "a.mp4", 0, 0, 100, kSourceTicks).value();
    const ClipId b = document.add_clip(track, "b.mp4", 0, 100, 100, kSourceTicks).value();

    std::vector<Clip> rewritten = document.find_track(track)->clips;
    rewritten[0].duration = 150;
    rewritten[1].start = 150;

    ASSERT_TRUE(document.replace_track_clips(track, rewritten).has_value());
    EXPECT_EQ(document.find_clip(a)->duration, 150);
    EXPECT_EQ(document.find_clip(b)->start, 150);
}

TEST(Document, ReplaceTrackClipsRefusesAnythingThatChangesTheIdSet) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    ASSERT_TRUE(document.add_clip(track, "a.mp4", 0, 0, 100, kSourceTicks).has_value());
    ASSERT_TRUE(document.add_clip(track, "b.mp4", 0, 100, 100, kSourceTicks).has_value());
    const std::vector<Clip> original = document.find_track(track)->clips;

    std::vector<Clip> shorter = original;
    shorter.pop_back();
    EXPECT_TRUE(document.replace_track_clips(track, shorter).has_error()) << "dropped a clip";

    std::vector<Clip> renumbered = original;
    renumbered[1].id = ClipId{999};
    EXPECT_TRUE(document.replace_track_clips(track, renumbered).has_error()) << "invented an id";

    std::vector<Clip> duplicated = original;
    duplicated[1].id = duplicated[0].id;
    EXPECT_TRUE(document.replace_track_clips(track, duplicated).has_error()) << "repeated an id";

    EXPECT_EQ(document.find_track(track)->clips, original) << "every refusal must change nothing";
}

TEST(Document, ReplaceTrackClipsIsAllOrNothing) {
    // The reason the primitive exists: a ripple moves several clips, and a
    // half-applied ripple cannot be undone because nothing recorded how far it
    // got. The last clip here is illegal, so none of it may land.
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    ASSERT_TRUE(document.add_clip(track, "a.mp4", 0, 0, 100, kSourceTicks).has_value());
    ASSERT_TRUE(document.add_clip(track, "b.mp4", 0, 100, 100, 100).has_value());
    const std::vector<Clip> original = document.find_track(track)->clips;

    std::vector<Clip> attempted = original;
    attempted[0].duration = 50;
    attempted[1].start = 50;
    attempted[1].duration = 101;  // one tick more media than "b.mp4" has

    const auto replaced = document.replace_track_clips(track, attempted);
    ASSERT_TRUE(replaced.has_error());
    EXPECT_EQ(document.find_track(track)->clips, original)
        << "the legal part of a refused replacement must not survive";
}

TEST(Document, ReplaceTrackClipsRefusesOverlap) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    ASSERT_TRUE(document.add_clip(track, "a.mp4", 0, 0, 100, kSourceTicks).has_value());
    ASSERT_TRUE(document.add_clip(track, "b.mp4", 0, 100, 100, kSourceTicks).has_value());

    std::vector<Clip> overlapping = document.find_track(track)->clips;
    overlapping[1].start = 99;
    EXPECT_TRUE(document.replace_track_clips(track, overlapping).has_error());
}

TEST(Document, ReplaceTrackClipsSortsTheResult) {
    // A trim can reorder nothing, but a caller is free to hand the vector back
    // in any order, and the document's ordering invariant is what makes
    // serialisation canonical.
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    const ClipId a = document.add_clip(track, "a.mp4", 0, 0, 100, kSourceTicks).value();
    const ClipId b = document.add_clip(track, "b.mp4", 0, 100, 100, kSourceTicks).value();

    std::vector<Clip> reversed = document.find_track(track)->clips;
    std::swap(reversed[0], reversed[1]);
    ASSERT_TRUE(document.replace_track_clips(track, reversed).has_value());

    EXPECT_EQ(document.find_track(track)->clips[0].id, a);
    EXPECT_EQ(document.find_track(track)->clips[1].id, b);
}

TEST(Document, RejectsANonPositiveDuration) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    EXPECT_TRUE(document.add_clip(track, "a.mp4", 0, 0, 0, kSourceTicks).has_error());
    EXPECT_TRUE(document.add_clip(track, "a.mp4", 0, 0, -1, kSourceTicks).has_error());
}

TEST(Document, RejectsNegativePositions) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    EXPECT_TRUE(document.add_clip(track, "a.mp4", 0, -1, 100, kSourceTicks).has_error());
    EXPECT_TRUE(document.add_clip(track, "a.mp4", -1, 0, 100, kSourceTicks).has_error());
}

TEST(Document, RejectsAClipOnAMissingTrack) {
    Document document = make_document();
    const auto clip = document.add_clip(TrackId{999}, "a.mp4", 0, 0, 100, kSourceTicks);
    ASSERT_TRUE(clip.has_error());
    EXPECT_EQ(clip.error().code(), Errc::not_found);
}

TEST(Document, RefusesOverlappingClips) {
    // Two clips occupying the same instant on one track have no defined render
    // result, so the model refuses rather than letting the renderer decide.
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    ASSERT_TRUE(document.add_clip(track, "a.mp4", 0, 100, 100, kSourceTicks).has_value());

    EXPECT_TRUE(document.add_clip(track, "b.mp4", 0, 150, 100, kSourceTicks).has_error()) << "overlaps the tail";
    EXPECT_TRUE(document.add_clip(track, "b.mp4", 0, 50, 100, kSourceTicks).has_error()) << "overlaps the head";
    EXPECT_TRUE(document.add_clip(track, "b.mp4", 0, 120, 10, kSourceTicks).has_error()) << "wholly inside";
    EXPECT_TRUE(document.add_clip(track, "b.mp4", 0, 50, 200, kSourceTicks).has_error()) << "wholly contains";
}

TEST(Document, AllowsButtJoinedClips) {
    // Clips that touch end-to-start are the normal case in an edit, not an
    // overlap. Getting this boundary wrong makes ordinary cutting impossible.
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    ASSERT_TRUE(document.add_clip(track, "a.mp4", 0, 100, 100, kSourceTicks).has_value());
    EXPECT_TRUE(document.add_clip(track, "b.mp4", 0, 200, 100, kSourceTicks).has_value())
        << "a clip starting exactly where the previous ends must be allowed";
    EXPECT_TRUE(document.add_clip(track, "c.mp4", 0, 0, 100, kSourceTicks).has_value())
        << "a clip ending exactly where the next begins must be allowed";
}

TEST(Document, AllowsOverlapAcrossDifferentTracks) {
    Document document = make_document();
    const TrackId video = document.add_track(TrackKind::video, "V1").value();
    const TrackId audio = document.add_track(TrackKind::audio, "A1").value();
    ASSERT_TRUE(document.add_clip(video, "a.mp4", 0, 0, 100, kSourceTicks).has_value());
    EXPECT_TRUE(document.add_clip(audio, "a.mp4", 0, 0, 100, kSourceTicks).has_value())
        << "tracks are independent lanes";
}

TEST(Document, KeepsClipsOrderedByStart) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    ASSERT_TRUE(document.add_clip(track, "c.mp4", 0, 200, 50, kSourceTicks).has_value());
    ASSERT_TRUE(document.add_clip(track, "a.mp4", 0, 0, 50, kSourceTicks).has_value());
    ASSERT_TRUE(document.add_clip(track, "b.mp4", 0, 100, 50, kSourceTicks).has_value());

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
    const ClipId id = document.add_clip(track, "a.mp4", 42, 300, 150, kSourceTicks).value();
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
    ASSERT_TRUE(document.add_clip(track, "a.mp4", 0, 0, 100, kSourceTicks).has_value());
    ASSERT_TRUE(document.add_clip(track, "b.mp4", 0, 100, 100, kSourceTicks).has_value());

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
    ASSERT_TRUE(document.insert_track_at(1, removed.value()).has_value());

    ASSERT_EQ(document.tracks().size(), 3u);
    EXPECT_EQ(document.tracks()[0].id, first);
    EXPECT_EQ(document.tracks()[1].id, second) << "restored track landed in the wrong place";
    EXPECT_EQ(document.tracks()[2].id, third);
}

TEST(Document, RefusesToInsertADuplicateId) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    const ClipId id = document.add_clip(track, "a.mp4", 0, 0, 100, kSourceTicks).value();

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
    const ClipId id = document.add_clip(track, "a.mp4", 0, 0, 100, kSourceTicks).value();

    ASSERT_TRUE(document.move_clip(id, track, 500).has_value());
    EXPECT_EQ(document.find_clip(id)->start, 500);
    EXPECT_EQ(document.find_clip(id)->duration, 100) << "moving must not resize";
}

TEST(Document, MovesAClipBetweenTracks) {
    Document document = make_document();
    const TrackId from = document.add_track(TrackKind::video, "V1").value();
    const TrackId to = document.add_track(TrackKind::video, "V2").value();
    const ClipId id = document.add_clip(from, "a.mp4", 7, 0, 100, kSourceTicks).value();

    ASSERT_TRUE(document.move_clip(id, to, 200).has_value());
    EXPECT_EQ(document.track_of_clip(id)->id, to);
    EXPECT_EQ(document.find_clip(id)->source_in, 7) << "moving must not alter the source offset";
    EXPECT_TRUE(document.find_track(from)->clips.empty());
}

TEST(Document, RefusesAMoveThatWouldOverlap) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    const ClipId first = document.add_clip(track, "a.mp4", 0, 0, 100, kSourceTicks).value();
    ASSERT_TRUE(document.add_clip(track, "b.mp4", 0, 200, 100, kSourceTicks).has_value());

    const auto moved = document.move_clip(first, track, 250);
    ASSERT_TRUE(moved.has_error());
    EXPECT_EQ(document.find_clip(first)->start, 0) << "a refused move must change nothing";
}

TEST(Document, TrimsAClip) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    const ClipId id = document.add_clip(track, "a.mp4", 0, 0, 100, kSourceTicks).value();

    ASSERT_TRUE(document.set_clip_bounds(id, 30, 30, 70).has_value());
    const Clip* clip = document.find_clip(id);
    EXPECT_EQ(clip->source_in, 30);
    EXPECT_EQ(clip->start, 30);
    EXPECT_EQ(clip->duration, 70);
}

TEST(Document, RefusesATrimThatWouldOverlapANeighbour) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    const ClipId first = document.add_clip(track, "a.mp4", 0, 0, 100, kSourceTicks).value();
    ASSERT_TRUE(document.add_clip(track, "b.mp4", 0, 100, 100, kSourceTicks).has_value());

    const auto trimmed = document.set_clip_bounds(first, 0, 0, 150);
    ASSERT_TRUE(trimmed.has_error());
    EXPECT_EQ(document.find_clip(first)->duration, 100) << "a refused trim must change nothing";
}

// --- links (ADR 011) ---------------------------------------------------------

TEST(Document, LinksAlignedClipsOnDifferentTracks) {
    Document document = make_document();
    const TrackId video = document.add_track(TrackKind::video, "V1").value();
    const TrackId audio = document.add_track(TrackKind::audio, "A1").value();
    const ClipId picture = document.add_clip(video, "a.mp4", 0, 100, 50, kSourceTicks).value();
    const ClipId sound = document.add_clip(audio, "a.wav", 0, 100, 50, kSourceTicks).value();

    const auto link = document.link_clips({picture, sound});
    ASSERT_TRUE(link.has_value()) << link.error().to_string();
    EXPECT_EQ(document.find_clip(picture)->link, link.value());
    EXPECT_EQ(document.find_clip(sound)->link, link.value());
    EXPECT_EQ(document.linked_clips(picture).size(), 2u);
    EXPECT_EQ(document.linked_clips(sound).size(), 2u);
}

TEST(Document, RefusesToLinkMisalignedClips) {
    // The invariant the whole design rests on: with aligned members a trim is
    // one delta applied to each, and drift cannot be represented.
    Document document = make_document();
    const TrackId video = document.add_track(TrackKind::video, "V1").value();
    const TrackId audio = document.add_track(TrackKind::audio, "A1").value();
    const ClipId picture = document.add_clip(video, "a.mp4", 0, 100, 50, kSourceTicks).value();
    const ClipId late = document.add_clip(audio, "a.wav", 0, 101, 50, kSourceTicks).value();
    const ClipId longer = document.add_clip(audio, "b.wav", 0, 200, 51, kSourceTicks).value();

    EXPECT_TRUE(document.link_clips({picture, late}).has_error()) << "starts differ";
    EXPECT_TRUE(document.link_clips({picture, longer}).has_error()) << "durations differ";
    EXPECT_FALSE(document.find_clip(picture)->link.is_valid())
        << "a refused link must leave no trace";
}

TEST(Document, RefusesToLinkTwoClipsOnOneTrack) {
    Document document = make_document();
    const TrackId video = document.add_track(TrackKind::video, "V1").value();
    const ClipId first = document.add_clip(video, "a.mp4", 0, 0, 50, kSourceTicks).value();
    // Same span is impossible on one track, so this can never be both aligned
    // and on one track -- but the check must not depend on that coincidence.
    const ClipId second = document.add_clip(video, "b.mp4", 0, 100, 50, kSourceTicks).value();
    EXPECT_TRUE(document.link_clips({first, second}).has_error());
}

TEST(Document, RefusesADegenerateOrUnknownLink) {
    Document document = make_document();
    const TrackId video = document.add_track(TrackKind::video, "V1").value();
    const ClipId only = document.add_clip(video, "a.mp4", 0, 0, 50, kSourceTicks).value();

    EXPECT_TRUE(document.link_clips({}).has_error()) << "nothing to link";
    EXPECT_TRUE(document.link_clips({only}).has_error()) << "a link needs two";
    EXPECT_EQ(document.link_clips({only, ClipId{999}}).error().code(), Errc::not_found);
}

TEST(Document, RefusesToLinkAClipThatIsAlreadyLinked) {
    Document document = make_document();
    const TrackId video = document.add_track(TrackKind::video, "V1").value();
    const TrackId a1 = document.add_track(TrackKind::audio, "A1").value();
    const TrackId a2 = document.add_track(TrackKind::audio, "A2").value();
    const ClipId picture = document.add_clip(video, "a.mp4", 0, 0, 50, kSourceTicks).value();
    const ClipId left = document.add_clip(a1, "l.wav", 0, 0, 50, kSourceTicks).value();
    const ClipId right = document.add_clip(a2, "r.wav", 0, 0, 50, kSourceTicks).value();

    ASSERT_TRUE(document.link_clips({picture, left}).has_value());
    EXPECT_EQ(document.link_clips({picture, right}).error().code(), Errc::already_exists);
}

TEST(Document, LinksSpendAnIdSoUndoCanGiveItBack) {
    Document document = make_document();
    const TrackId video = document.add_track(TrackKind::video, "V1").value();
    const TrackId audio = document.add_track(TrackKind::audio, "A1").value();
    const ClipId picture = document.add_clip(video, "a.mp4", 0, 0, 50, kSourceTicks).value();
    const ClipId sound = document.add_clip(audio, "a.wav", 0, 0, 50, kSourceTicks).value();

    const std::uint64_t before = document.next_id();
    ASSERT_TRUE(document.link_clips({picture, sound}).has_value());
    EXPECT_EQ(document.next_id(), before + 1) << "a link id comes from the one counter, like all";
}

TEST(Document, UnlinkLeavesTheClipsWhereTheyAre) {
    Document document = make_document();
    const TrackId video = document.add_track(TrackKind::video, "V1").value();
    const TrackId audio = document.add_track(TrackKind::audio, "A1").value();
    const ClipId picture = document.add_clip(video, "a.mp4", 0, 100, 50, kSourceTicks).value();
    const ClipId sound = document.add_clip(audio, "a.wav", 0, 100, 50, kSourceTicks).value();
    const LinkId link = document.link_clips({picture, sound}).value();

    ASSERT_TRUE(document.unlink(link).has_value());
    EXPECT_FALSE(document.find_clip(picture)->link.is_valid());
    EXPECT_FALSE(document.find_clip(sound)->link.is_valid());
    EXPECT_EQ(document.find_clip(picture)->start, 100) << "unlinking is not a move";
    EXPECT_EQ(document.unlink(link).error().code(), Errc::not_found) << "already gone";
}

TEST(Document, RemovingAMemberLeavesTheRestLinked) {
    // A group of one behaves exactly as an unlinked clip, which is what keeps
    // clip removal from having to rewrite anyone else's state.
    Document document = make_document();
    const TrackId video = document.add_track(TrackKind::video, "V1").value();
    const TrackId audio = document.add_track(TrackKind::audio, "A1").value();
    const ClipId picture = document.add_clip(video, "a.mp4", 0, 0, 50, kSourceTicks).value();
    const ClipId sound = document.add_clip(audio, "a.wav", 0, 0, 50, kSourceTicks).value();
    ASSERT_TRUE(document.link_clips({picture, sound}).has_value());

    ASSERT_TRUE(document.remove_clip(sound).has_value());
    EXPECT_EQ(document.linked_clips(picture), std::vector<ClipId>{picture});
    EXPECT_TRUE(document.find_clip(picture)->link.is_valid()) << "the group id survives";
}

TEST(Document, TogglesFlags) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    const ClipId clip = document.add_clip(track, "a.mp4", 0, 0, 100, kSourceTicks).value();

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
