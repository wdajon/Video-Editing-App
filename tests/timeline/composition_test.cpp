// "What is on screen at frame N" -- the arithmetic the Program monitor, playback
// and the export path all depend on, tested with no decoder and no GPU.
//
// The source-frame calculation is the part worth guarding hardest: an off-by-one
// here shows the wrong picture, and nothing downstream can detect it.

#include "rf/timeline/composition.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using rf::Errc;
using rf::media::Rational;
using rf::timeline::ClipId;
using rf::timeline::Document;
using rf::timeline::Layer;
using rf::timeline::TrackId;
using rf::timeline::TrackKind;
using rf::timeline::Ticks;
using rf::timeline::layers_at;
using rf::timeline::sequence_end_frame;

constexpr Ticks kFrame = 3000;  // 30 fps at a 1/90000 base

Document make_document() {
    return Document::create(Rational{1, 90000}, Rational{30, 1}).value();
}

TEST(Composition, ShowsTheClipUnderThePlayhead) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    const ClipId clip =
        document.add_clip(track, "a.mov", 10 * kFrame, 0, 30 * kFrame, 90 * kFrame).value();

    const auto layers = layers_at(document, 0);
    ASSERT_TRUE(layers.has_value()) << layers.error().to_string();
    ASSERT_EQ(layers.value().size(), 1u);
    EXPECT_EQ(layers.value()[0].clip, clip);
    EXPECT_EQ(layers.value()[0].source, "a.mov");
    EXPECT_EQ(layers.value()[0].source_frame, 10) << "the clip starts 10 frames into its source";
}

TEST(Composition, WalksTheSourceOneFramePerTimelineFrame) {
    // The calculation an off-by-one would ruin invisibly.
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    ASSERT_TRUE(
        document.add_clip(track, "a.mov", 10 * kFrame, 5 * kFrame, 30 * kFrame, 90 * kFrame)
            .has_value());

    EXPECT_EQ(layers_at(document, 5).value()[0].source_frame, 10) << "first frame of the clip";
    EXPECT_EQ(layers_at(document, 6).value()[0].source_frame, 11);
    EXPECT_EQ(layers_at(document, 20).value()[0].source_frame, 25);
    EXPECT_EQ(layers_at(document, 34).value()[0].source_frame, 39) << "last frame of the clip";
}

TEST(Composition, AClipDoesNotCoverTheFrameItEndsOn) {
    // Half-open, so butt-joined clips show exactly one picture at the join --
    // not two, and not a one-frame hole.
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    const ClipId first =
        document.add_clip(track, "a.mov", 0, 0, 10 * kFrame, 90 * kFrame).value();
    const ClipId second =
        document.add_clip(track, "b.mov", 0, 10 * kFrame, 10 * kFrame, 90 * kFrame).value();

    EXPECT_EQ(layers_at(document, 9).value()[0].clip, first);
    ASSERT_EQ(layers_at(document, 10).value().size(), 1u) << "exactly one picture at the join";
    EXPECT_EQ(layers_at(document, 10).value()[0].clip, second);
}

TEST(Composition, AGapShowsNothingRatherThanTheLastFrame) {
    // Empty is a legitimate answer. A renderer that held the previous frame here
    // would make a stale picture indistinguishable from a live one.
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    ASSERT_TRUE(document.add_clip(track, "a.mov", 0, 0, 10 * kFrame, 90 * kFrame).has_value());
    ASSERT_TRUE(
        document.add_clip(track, "b.mov", 0, 20 * kFrame, 10 * kFrame, 90 * kFrame).has_value());

    const auto in_gap = layers_at(document, 15);
    ASSERT_TRUE(in_gap.has_value());
    EXPECT_TRUE(in_gap.value().empty());

    const auto past_the_end = layers_at(document, 500);
    ASSERT_TRUE(past_the_end.has_value());
    EXPECT_TRUE(past_the_end.value().empty());
}

TEST(Composition, StacksTracksBottomFirst) {
    // Compositing order: V1 is the base and each higher track draws over it.
    Document document = make_document();
    const TrackId v1 = document.add_track(TrackKind::video, "V1").value();
    const TrackId v2 = document.add_track(TrackKind::video, "V2").value();
    ASSERT_TRUE(document.add_clip(v1, "under.mov", 0, 0, 30 * kFrame, 90 * kFrame).has_value());
    ASSERT_TRUE(document.add_clip(v2, "over.mov", 0, 0, 30 * kFrame, 90 * kFrame).has_value());

    const auto layers = layers_at(document, 5).value();
    ASSERT_EQ(layers.size(), 2u);
    EXPECT_EQ(layers[0].track, v1) << "the base comes first";
    EXPECT_EQ(layers[1].track, v2);
}

TEST(Composition, LeavesOutAudioTracks) {
    Document document = make_document();
    const TrackId video = document.add_track(TrackKind::video, "V1").value();
    const TrackId audio = document.add_track(TrackKind::audio, "A1").value();
    ASSERT_TRUE(document.add_clip(video, "a.mov", 0, 0, 30 * kFrame, 90 * kFrame).has_value());
    ASSERT_TRUE(document.add_clip(audio, "a.wav", 0, 0, 30 * kFrame, 90 * kFrame).has_value());

    const auto layers = layers_at(document, 5).value();
    ASSERT_EQ(layers.size(), 1u) << "audio carries no picture";
    EXPECT_EQ(layers[0].source, "a.mov");
}

TEST(Composition, LeavesOutDisabledClips) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    const ClipId clip =
        document.add_clip(track, "a.mov", 0, 0, 30 * kFrame, 90 * kFrame).value();
    ASSERT_TRUE(document.set_clip_enabled(clip, false).has_value());

    EXPECT_TRUE(layers_at(document, 5).value().empty()) << "that is what disabling one means";
}

TEST(Composition, ADisabledClipDoesNotRevealTheOneBehindItOnItsOwnTrack) {
    // Clips on a track never overlap, so a disabled clip leaves a hole rather
    // than uncovering something. Checked because "skip and keep looking" is the
    // natural way to write this loop and is wrong.
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    const ClipId first =
        document.add_clip(track, "a.mov", 0, 0, 10 * kFrame, 90 * kFrame).value();
    ASSERT_TRUE(
        document.add_clip(track, "b.mov", 0, 10 * kFrame, 10 * kFrame, 90 * kFrame).has_value());
    ASSERT_TRUE(document.set_clip_enabled(first, false).has_value());

    EXPECT_TRUE(layers_at(document, 5).value().empty());
    EXPECT_EQ(layers_at(document, 12).value().size(), 1u) << "the next clip is unaffected";
}

TEST(Composition, ReportsAFrameThatIsNotRepresentable) {
    // Rather than wrapping and showing the wrong part of the timeline.
    Document document = make_document();
    EXPECT_TRUE(layers_at(document, -1).has_error());
    EXPECT_EQ(layers_at(document, std::numeric_limits<std::int64_t>::max()).error().code(),
              Errc::invalid_argument);
}

TEST(Composition, SequenceEndIsTheLastFrameWithAnythingOnIt) {
    Document document = make_document();
    EXPECT_EQ(sequence_end_frame(document), 0) << "an empty sequence ends at zero";

    const TrackId v1 = document.add_track(TrackKind::video, "V1").value();
    const TrackId a1 = document.add_track(TrackKind::audio, "A1").value();
    ASSERT_TRUE(document.add_clip(v1, "a.mov", 0, 0, 30 * kFrame, 90 * kFrame).has_value());
    EXPECT_EQ(sequence_end_frame(document), 30);

    // Audio counts towards the length even though it carries no picture.
    ASSERT_TRUE(
        document.add_clip(a1, "a.wav", 0, 0, 50 * kFrame, 90 * kFrame).has_value());
    EXPECT_EQ(sequence_end_frame(document), 50);
}

TEST(Composition, IsExactAtAnAwkwardFrameRate) {
    // 29.97: one frame is 3003 ticks. A calculation that assumed 3000 would
    // drift a frame every few seconds and show the wrong picture.
    Document document = Document::create(Rational{1, 90000}, Rational{30000, 1001}).value();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    ASSERT_TRUE(
        document.add_clip(track, "a.mov", 100 * 3003, 0, 200 * 3003, 400 * 3003).has_value());

    EXPECT_EQ(layers_at(document, 0).value()[0].source_frame, 100);
    EXPECT_EQ(layers_at(document, 150).value()[0].source_frame, 250);
    EXPECT_EQ(layers_at(document, 199).value()[0].source_frame, 299);
}

}  // namespace
