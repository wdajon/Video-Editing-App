// The demo timeline is a fixture for a human, so what it has to guarantee is
// that a person sitting down with it can exercise the whole trim set without
// running out of media or hitting an edge case in the first ten keystrokes.

#include "rf/app/demo_timeline.hpp"

#include <gtest/gtest.h>

#include "rf/edit/command_map.hpp"
#include "rf/edit/editor.hpp"
#include "rf/timeline/trim.hpp"

namespace {

using rf::Errc;
using rf::app::build_demo_timeline;
using rf::edit::Action;
using rf::edit::EditState;
using rf::edit::Editor;
using rf::edit::Tool;
using rf::media::Rational;
using rf::timeline::Clip;
using rf::timeline::CommandStack;
using rf::timeline::Document;
using rf::timeline::Track;

Document demo() {
    Document document = Document::create(Rational{1, 90000}, Rational{30, 1}).value();
    EXPECT_TRUE(build_demo_timeline(document).has_value());
    return document;
}

TEST(DemoTimeline, BuildsAPictureTrackAndASoundTrack) {
    const Document document = demo();
    ASSERT_EQ(document.tracks().size(), 2u);
    EXPECT_EQ(document.tracks()[0].kind, rf::timeline::TrackKind::video);
    EXPECT_EQ(document.tracks()[1].kind, rf::timeline::TrackKind::audio);
    EXPECT_EQ(document.clip_count(), 8u);
}

TEST(DemoTimeline, EveryClipIsButtJoinedToTheNext) {
    // Roll needs a butt-joined neighbour and slide needs two, so a demo with
    // gaps would refuse half the trim set on the first keypress.
    const Document document = demo();
    for (const Track& track : document.tracks()) {
        for (std::size_t i = 1; i < track.clips.size(); ++i) {
            EXPECT_EQ(track.clips[i].start,
                      track.clips[i - 1].start + track.clips[i - 1].duration)
                << "gap before clip " << i << " on " << track.name;
        }
    }
}

TEST(DemoTimeline, EveryClipHasHandlesAtBothEnds) {
    const Document document = demo();
    for (const Track& track : document.tracks()) {
        for (const Clip& clip : track.clips) {
            EXPECT_GT(clip.source_in, 0) << "no handle before " << clip.source;
            EXPECT_GT(clip.source_duration - clip.source_in - clip.duration, 0)
                << "no handle after " << clip.source;
        }
    }
}

TEST(DemoTimeline, PictureAndSoundAreLinked) {
    const Document document = demo();
    for (const Clip& clip : document.tracks()[0].clips) {
        EXPECT_EQ(document.linked_clips(clip.id).size(), 2u)
            << clip.source << " is not linked to its sound";
    }
}

TEST(DemoTimeline, EveryTrimInTheSetSucceedsOnTheFirstPress) {
    // The point of the fixture: someone trying the keyboard should not meet a
    // refusal before they have seen anything work.
    for (const auto [tool, name] :
         {std::pair{Tool::ripple, "ripple"}, std::pair{Tool::roll, "roll"},
          std::pair{Tool::slip, "slip"}, std::pair{Tool::slide, "slide"}}) {
        Document document = demo();
        CommandStack stack;
        EditState state;
        state.track = document.tracks().front().id;
        // The second clip, so slide and roll both have neighbours either side.
        state.clip = document.tracks().front().clips[1].id;
        state.tool = tool;
        Editor editor{document, stack, state};

        EXPECT_TRUE(editor.perform(Action::trim_forward).has_value()) << name << " forward";
        EXPECT_TRUE(editor.perform(Action::trim_backward_many).has_value()) << name << " back 5";
    }
}

TEST(DemoTimeline, RefusesToScribbleOnADocumentThatAlreadyHasContent) {
    Document document = demo();
    const auto again = build_demo_timeline(document);
    ASSERT_TRUE(again.has_error());
    EXPECT_EQ(again.error().code(), Errc::already_exists);
    EXPECT_EQ(document.clip_count(), 8u) << "a refused build must change nothing";
}

TEST(DemoTimeline, UsesTheDocumentsOwnFrameRateRatherThanAssumingOne) {
    // Built at 29.97, where a frame is 3003 ticks rather than 3000. A demo that
    // hardcoded 3000 would put every clip boundary a fraction of a frame off.
    Document ntsc = Document::create(Rational{1, 90000}, Rational{30000, 1001}).value();
    ASSERT_TRUE(build_demo_timeline(ntsc).has_value());
    for (const Clip& clip : ntsc.tracks().front().clips) {
        EXPECT_EQ(clip.duration % ntsc.ticks_per_frame(), 0) << "not a whole number of frames";
        EXPECT_EQ(clip.start % ntsc.ticks_per_frame(), 0);
    }
}

}  // namespace
