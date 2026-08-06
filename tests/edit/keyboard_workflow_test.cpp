// M4's exit gate, as a test: the full trim set driven by keyboard only.
//
// Every edit below goes through `Editor::press` with a key chord looked up in
// the command map. Nothing calls `make_trim` directly, and nothing names a tick
// count -- if a binding is wrong, a tool does not apply, or a frame is not a
// whole number of ticks, these fail.
//
// What this does NOT prove: that a real key press in a real window reaches this
// code. That is the Qt wiring, and it is the rest of M4.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "rf/edit/command_map.hpp"
#include "rf/edit/editor.hpp"
#include "rf/timeline/serialise.hpp"
#include "rf/timeline/trim.hpp"

namespace {

using rf::Errc;
using rf::edit::Action;
using rf::edit::CommandMap;
using rf::edit::EditState;
using rf::edit::Editor;
using rf::edit::KeyChord;
using rf::edit::Tool;
using rf::edit::parse_chord;
using rf::media::Rational;
using rf::timeline::Clip;
using rf::timeline::ClipId;
using rf::timeline::CommandStack;
using rf::timeline::Document;
using rf::timeline::Ticks;
using rf::timeline::TrackId;
using rf::timeline::TrackKind;
using rf::timeline::serialise;

/// 30 fps at the conventional 1/90000 base, so one frame is exactly 3000 ticks
/// and every expected value below can be read as frames.
constexpr Ticks kFrame = 3000;

/// Three butt-joined one-second clips, each cut from the middle of a three
/// second source so every trim has room in both directions.
struct Session {
    Document document = Document::create(Rational{1, 90000}, Rational{30, 1}).value();
    CommandStack stack;
    EditState state;
    TrackId track;
    ClipId a;
    ClipId b;
    ClipId c;

    Session() {
        track = document.add_track(TrackKind::video, "V1").value();
        a = document.add_clip(track, "a.mp4", 30 * kFrame, 0, 30 * kFrame, 90 * kFrame).value();
        b = document.add_clip(track, "b.mp4", 30 * kFrame, 30 * kFrame, 30 * kFrame,
                              90 * kFrame)
                .value();
        c = document.add_clip(track, "c.mp4", 30 * kFrame, 60 * kFrame, 30 * kFrame,
                              90 * kFrame)
                .value();
        state.track = track;
        state.clip = b;
    }

    [[nodiscard]] Editor editor() { return Editor{document, stack, state}; }
    [[nodiscard]] const Clip& clip(ClipId id) const { return *document.find_clip(id); }
};

/// Presses a chord, failing the test with the error if it is refused.
void press(Session& session, const CommandMap& map, std::string_view chord) {
    const auto parsed = parse_chord(chord);
    ASSERT_TRUE(parsed.has_value()) << parsed.error().to_string();
    Editor editor = session.editor();
    const auto result = editor.press(map, parsed.value());
    ASSERT_TRUE(result.has_value()) << chord << ": " << result.error().to_string();
}

// --- the gate ----------------------------------------------------------------

TEST(KeyboardWorkflow, RippleTrimsTheOutPointByOneFrame) {
    Session session;
    const CommandMap map = CommandMap::defaults();

    press(session, map, "B");    // ripple edit tool
    press(session, map, "]");    // act on the out point
    press(session, map, "Ctrl+Right");

    EXPECT_EQ(session.state.tool, Tool::ripple);
    EXPECT_EQ(session.clip(session.b).duration, 31 * kFrame);
    EXPECT_EQ(session.clip(session.c).start, 61 * kFrame) << "the ripple must carry c along";
}

TEST(KeyboardWorkflow, RippleTrimsTheInPointBackward) {
    Session session;
    const CommandMap map = CommandMap::defaults();

    press(session, map, "B");
    press(session, map, "[");
    press(session, map, "Ctrl+Left");

    EXPECT_EQ(session.clip(session.b).source_in, 29 * kFrame);
    EXPECT_EQ(session.clip(session.b).duration, 31 * kFrame);
    EXPECT_EQ(session.clip(session.b).start, 30 * kFrame) << "a ripple-in keeps the start";
}

TEST(KeyboardWorkflow, RollMovesTheSharedEditPoint) {
    Session session;
    const CommandMap map = CommandMap::defaults();

    press(session, map, "N");  // rolling edit tool
    press(session, map, "Ctrl+Right");

    EXPECT_EQ(session.state.tool, Tool::roll);
    EXPECT_EQ(session.clip(session.b).duration, 31 * kFrame);
    EXPECT_EQ(session.clip(session.c).start, 61 * kFrame);
    EXPECT_EQ(session.clip(session.c).duration, 29 * kFrame);
    EXPECT_EQ(session.clip(session.c).start + session.clip(session.c).duration, 90 * kFrame)
        << "a roll must not change the sequence length";
}

TEST(KeyboardWorkflow, SlipMovesTheContentAndNothingElse) {
    Session session;
    const CommandMap map = CommandMap::defaults();

    press(session, map, "Y");  // slip tool
    press(session, map, "Ctrl+Left");

    EXPECT_EQ(session.state.tool, Tool::slip);
    EXPECT_EQ(session.clip(session.b).source_in, 29 * kFrame);
    EXPECT_EQ(session.clip(session.b).start, 30 * kFrame);
    EXPECT_EQ(session.clip(session.b).duration, 30 * kFrame);
}

TEST(KeyboardWorkflow, SlideMovesTheClipAndItsNeighboursAbsorbIt) {
    Session session;
    const CommandMap map = CommandMap::defaults();

    press(session, map, "U");  // slide tool
    press(session, map, "Ctrl+Right");

    EXPECT_EQ(session.state.tool, Tool::slide);
    EXPECT_EQ(session.clip(session.b).start, 31 * kFrame);
    EXPECT_EQ(session.clip(session.a).duration, 31 * kFrame);
    EXPECT_EQ(session.clip(session.c).start, 61 * kFrame);
    EXPECT_EQ(session.clip(session.c).duration, 29 * kFrame);
}

TEST(KeyboardWorkflow, TheWholeTrimSetFromTheKeyboardAndBackAgain) {
    // The gate in one test: all four operations performed with nothing but key
    // chords, then undone with nothing but key chords, ending byte-identical.
    Session session;
    const CommandMap map = CommandMap::defaults();
    const std::string before = serialise(session.document);

    press(session, map, "B");
    press(session, map, "]");
    press(session, map, "Ctrl+Shift+Right");  // ripple out, large offset
    press(session, map, "[");
    press(session, map, "Ctrl+Right");        // ripple in
    press(session, map, "N");
    press(session, map, "Ctrl+Left");         // roll
    press(session, map, "Y");
    press(session, map, "Ctrl+Right");        // slip
    press(session, map, "U");
    press(session, map, "Ctrl+Left");         // slide

    EXPECT_NE(serialise(session.document), before);
    EXPECT_EQ(session.stack.undo_depth(), 5u) << "five edits, five undo entries";

    for (int i = 0; i < 5; ++i) {
        press(session, map, "Ctrl+Z");
    }
    EXPECT_EQ(serialise(session.document), before)
        << "undoing from the keyboard must return the document exactly";

    press(session, map, "Ctrl+Shift+Z");
    EXPECT_EQ(session.stack.undo_depth(), 1u) << "redo is bound too";
}

// --- frames, not ticks -------------------------------------------------------

TEST(KeyboardWorkflow, ALargeTrimMovesTheConfiguredNumberOfFrames) {
    Session session;
    const CommandMap map = CommandMap::defaults();
    ASSERT_EQ(session.state.large_trim_frames, 5) << "Premiere's default";

    press(session, map, "B");
    press(session, map, "]");
    press(session, map, "Ctrl+Shift+Right");
    EXPECT_EQ(session.clip(session.b).duration, 35 * kFrame);
}

TEST(KeyboardWorkflow, TheLargeTrimOffsetIsAPreferenceNotAConstant) {
    Session session;
    const CommandMap map = CommandMap::defaults();
    session.state.large_trim_frames = 12;

    press(session, map, "B");
    press(session, map, "]");
    press(session, map, "Ctrl+Shift+Right");
    EXPECT_EQ(session.clip(session.b).duration, 42 * kFrame);
}

TEST(KeyboardWorkflow, ATrimAtAnAwkwardFrameRateIsStillExact) {
    // 29.97 fps: one frame is 3003 ticks at a 1/90000 base, exactly. Ten trims
    // must land on frame 10 and not somewhere near it.
    Document document = Document::create(Rational{1, 90000}, Rational{30000, 1001}).value();
    ASSERT_EQ(document.ticks_per_frame(), 3003);

    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    const ClipId clip =
        document.add_clip(track, "a.mp4", 30 * 3003, 0, 30 * 3003, 90 * 3003).value();

    CommandStack stack;
    EditState state;
    state.track = track;
    state.clip = clip;
    state.tool = Tool::ripple;
    state.edge = rf::edit::Edge::out;
    Editor editor{document, stack, state};

    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(editor.perform(Action::trim_forward).has_value());
    }
    EXPECT_EQ(document.find_clip(clip)->duration, 40 * 3003)
        << "ten one-frame trims must be exactly ten frames";
}

// --- refusals ----------------------------------------------------------------

TEST(KeyboardWorkflow, TrimmingWithTheSelectionToolSaysWhyItDidNothing) {
    // A silent no-op here is indistinguishable from a broken key binding, and a
    // keyboard-only user has nothing else to go on.
    Session session;
    const CommandMap map = CommandMap::defaults();
    Editor editor = session.editor();

    ASSERT_EQ(session.state.tool, Tool::selection);
    const auto refused = editor.press(map, parse_chord("Ctrl+Right").value());
    ASSERT_TRUE(refused.has_error());
    EXPECT_NE(refused.error().to_string().find("does not trim"), std::string::npos)
        << refused.error().to_string();
    EXPECT_FALSE(session.stack.can_undo());
}

TEST(KeyboardWorkflow, TrimmingWithNothingSelectedIsRefused) {
    Session session;
    session.state.clip = ClipId{};
    session.state.tool = Tool::ripple;
    Editor editor = session.editor();

    const auto refused = editor.perform(Action::trim_forward);
    ASSERT_TRUE(refused.has_error());
    EXPECT_EQ(refused.error().code(), Errc::not_found);
}

TEST(KeyboardWorkflow, AnUnboundChordIsReportedRatherThanIgnored) {
    Session session;
    const CommandMap map = CommandMap::defaults();
    Editor editor = session.editor();

    const auto unbound = editor.press(map, parse_chord("F9").value());
    ASSERT_TRUE(unbound.has_error());
    EXPECT_EQ(unbound.error().code(), Errc::not_found);
}

TEST(KeyboardWorkflow, ATrimThatWouldPassTheMediaLimitIsRefusedNotClamped) {
    // The clip has 30 frames of tail. Asking for 5 more once it is exhausted
    // must fail rather than silently do nothing.
    Session session;
    const CommandMap map = CommandMap::defaults();
    press(session, map, "Y");  // slip: bounded by the source at both ends

    Editor editor = session.editor();
    ASSERT_TRUE(editor.perform(Action::trim_forward_many).has_value());
    for (int i = 0; i < 6; ++i) {
        (void)editor.perform(Action::trim_forward_many);
    }
    const Clip& b = session.clip(session.b);
    EXPECT_EQ(b.source_in + b.duration, b.source_duration) << "sitting on the limit";

    const auto refused = editor.perform(Action::trim_forward_many);
    ASSERT_TRUE(refused.has_error()) << "a trim with no room must say so";
}

// --- selection ---------------------------------------------------------------

TEST(KeyboardWorkflow, ArrowsWalkTheClipsOnATrack) {
    Session session;
    const CommandMap map = CommandMap::defaults();

    press(session, map, "Down");
    EXPECT_EQ(session.state.clip, session.c);

    press(session, map, "Up");
    EXPECT_EQ(session.state.clip, session.b);
}

TEST(KeyboardWorkflow, SelectingWithNothingSelectedLandsOnTheFirstClip) {
    Session session;
    session.state.clip = ClipId{};
    Editor editor = session.editor();

    ASSERT_TRUE(editor.perform(Action::select_previous_clip).has_value())
        << "a user with nothing selected must be able to start from the keyboard";
    EXPECT_EQ(session.state.clip, session.a);
}

TEST(KeyboardWorkflow, WalkingOffTheEndOfATrackIsRefusedRatherThanWrapping) {
    // Wrapping would move an edit somewhere the user was not looking.
    Session session;
    const CommandMap map = CommandMap::defaults();
    session.state.clip = session.c;
    Editor editor = session.editor();

    const auto refused = editor.perform(Action::select_next_clip);
    ASSERT_TRUE(refused.has_error());
    EXPECT_EQ(session.state.clip, session.c) << "the selection must not have moved";
}

TEST(KeyboardWorkflow, ShiftArrowsMoveBetweenTracks) {
    Session session;
    const TrackId audio = session.document.add_track(TrackKind::audio, "A1").value();
    const ClipId sound =
        session.document.add_clip(audio, "a.wav", 0, 0, 30 * kFrame, 90 * kFrame).value();
    const CommandMap map = CommandMap::defaults();

    press(session, map, "Shift+Down");
    EXPECT_EQ(session.state.track, audio);
    EXPECT_EQ(session.state.clip, sound) << "moving track selects its first clip";

    press(session, map, "Shift+Up");
    EXPECT_EQ(session.state.track, session.track);
}

// --- remapping ---------------------------------------------------------------

TEST(KeyboardWorkflow, ARemappedKeyPerformsTheTrim) {
    // The point of a command map: a user who wants ripple on R gets ripple on R,
    // and nothing else in the workflow changes.
    Session session;
    const auto map = CommandMap::parse(
        "reelforge-keymap/1\n"
        "bind R select_tool_ripple\n");
    ASSERT_TRUE(map.has_value()) << map.error().to_string();

    press(session, map.value(), "R");
    press(session, map.value(), "]");
    press(session, map.value(), "Ctrl+Right");
    EXPECT_EQ(session.clip(session.b).duration, 31 * kFrame);

    // B still works: binding an action to a second chord does not unbind it.
    press(session, map.value(), "B");
    EXPECT_EQ(session.state.tool, Tool::ripple);
}

}  // namespace
