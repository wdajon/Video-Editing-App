#include "rf/edit/command_map.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

using rf::Errc;
using rf::edit::Action;
using rf::edit::CommandMap;
using rf::edit::Key;
using rf::edit::KeyChord;
using rf::edit::Modifiers;
using rf::edit::parse_action;
using rf::edit::parse_chord;
using rf::edit::to_string;

Action bound_to(const CommandMap& map, std::string_view chord) {
    const auto parsed = parse_chord(chord);
    EXPECT_TRUE(parsed.has_value()) << chord;
    const auto action = map.lookup(parsed.value());
    EXPECT_TRUE(action.has_value()) << chord << " is not bound";
    return action.value_or(Action::undo);
}

// --- the defaults ------------------------------------------------------------

TEST(CommandMapTest, MatchesPremiereWhereverPremiereHasADefault) {
    // Read from Adobe's documentation, not remembered. See ADR 012.
    const CommandMap map = CommandMap::defaults();
    EXPECT_EQ(bound_to(map, "V"), Action::select_tool_selection);
    EXPECT_EQ(bound_to(map, "B"), Action::select_tool_ripple);
    EXPECT_EQ(bound_to(map, "N"), Action::select_tool_roll);
    EXPECT_EQ(bound_to(map, "Y"), Action::select_tool_slip);
    EXPECT_EQ(bound_to(map, "U"), Action::select_tool_slide);
    EXPECT_EQ(bound_to(map, "Ctrl+Left"), Action::trim_backward);
    EXPECT_EQ(bound_to(map, "Ctrl+Right"), Action::trim_forward);
    EXPECT_EQ(bound_to(map, "Ctrl+Shift+Left"), Action::trim_backward_many);
    EXPECT_EQ(bound_to(map, "Ctrl+Shift+Right"), Action::trim_forward_many);
}

TEST(CommandMapTest, EveryDefaultChordParses) {
    // The default table is written as text, so a typo in it would otherwise be a
    // silently missing binding rather than a build failure.
    const CommandMap map = CommandMap::defaults();
    EXPECT_EQ(map.size(), 37u) << map.serialise();
}

TEST(CommandMapTest, EveryTrimActionIsReachableFromTheKeyboard) {
    // The gate is "driven by keyboard only". An action with no chord is an
    // action a keyboard-only user cannot perform.
    const CommandMap map = CommandMap::defaults();
    for (const Action action :
         {Action::select_tool_selection, Action::select_tool_ripple, Action::select_tool_roll,
          Action::select_tool_slip, Action::select_tool_slide, Action::trim_backward,
          Action::trim_forward, Action::trim_backward_many, Action::trim_forward_many,
          Action::select_previous_clip, Action::select_next_clip, Action::select_previous_track,
          Action::select_next_track, Action::select_in_edge, Action::select_out_edge,
          Action::shuttle_forward, Action::shuttle_backward, Action::shuttle_stop,
          Action::shuttle_slow_forward, Action::shuttle_slow_backward,
          Action::undo, Action::redo}) {
        EXPECT_FALSE(map.chords_for(action).empty()) << to_string(action) << " has no key";
    }
}

TEST(CommandMapTest, MatchesAdobesTimelinePanelDefaultsExactly) {
    // Transcribed from Adobe's default-shortcuts page, Timeline panel section,
    // read 2026-08-06 and recorded in ADR 016. These are the commands that act
    // on the clip selection with no tool involved.
    const CommandMap map = CommandMap::defaults();

    EXPECT_EQ(bound_to(map, "Alt+Left"), Action::nudge_backward);
    EXPECT_EQ(bound_to(map, "Alt+Right"), Action::nudge_forward);
    EXPECT_EQ(bound_to(map, "Alt+Shift+Left"), Action::nudge_backward_many);
    EXPECT_EQ(bound_to(map, "Alt+Shift+Right"), Action::nudge_forward_many);

    EXPECT_EQ(bound_to(map, "Ctrl+Alt+Left"), Action::slip_backward);
    EXPECT_EQ(bound_to(map, "Ctrl+Alt+Right"), Action::slip_forward);
    EXPECT_EQ(bound_to(map, "Ctrl+Alt+Shift+Left"), Action::slip_backward_many);
    EXPECT_EQ(bound_to(map, "Ctrl+Alt+Shift+Right"), Action::slip_forward_many);

    EXPECT_EQ(bound_to(map, "Alt+,"), Action::slide_backward);
    EXPECT_EQ(bound_to(map, "Alt+."), Action::slide_forward);
    EXPECT_EQ(bound_to(map, "Alt+Shift+,"), Action::slide_backward_many);
    EXPECT_EQ(bound_to(map, "Alt+Shift+."), Action::slide_forward_many);

    EXPECT_EQ(bound_to(map, "Left"), Action::step_backward);
    EXPECT_EQ(bound_to(map, "Right"), Action::step_forward);
    EXPECT_EQ(bound_to(map, "Space"), Action::play_stop);
}

TEST(CommandMapTest, NoChordIsBoundTwice) {
    // Adding Adobe's defaults over an existing map is exactly where a silent
    // collision would happen: one binding would overwrite another and a key the
    // user relies on would quietly change meaning.
    const CommandMap map = CommandMap::defaults();
    const std::string text = map.serialise();
    std::size_t bindings = 0;
    for (const char character : text) {
        bindings += character == '\n' ? 1 : 0;
    }
    // One header line plus one line per binding, and `serialise` is keyed by
    // chord, so a collision would show up as a smaller map than the table.
    EXPECT_EQ(bindings - 1, map.size());
}

TEST(CommandMapTest, JKLAreWhereEveryEditorPutsThem) {
    const CommandMap map = CommandMap::defaults();
    EXPECT_EQ(bound_to(map, "J"), Action::shuttle_backward);
    EXPECT_EQ(bound_to(map, "K"), Action::shuttle_stop);
    EXPECT_EQ(bound_to(map, "L"), Action::shuttle_forward);
    EXPECT_EQ(bound_to(map, "Shift+J"), Action::shuttle_slow_backward);
    EXPECT_EQ(bound_to(map, "Shift+L"), Action::shuttle_slow_forward);
}

TEST(ActionTest, EveryActionRoundTripsThroughItsName) {
    for (const Action action :
         {Action::select_tool_selection, Action::select_tool_ripple, Action::select_tool_roll,
          Action::select_tool_slip, Action::select_tool_slide, Action::trim_backward,
          Action::trim_forward, Action::trim_backward_many, Action::trim_forward_many,
          Action::select_previous_clip, Action::select_next_clip, Action::select_previous_track,
          Action::select_next_track, Action::select_in_edge, Action::select_out_edge,
          Action::undo, Action::redo}) {
        const std::string_view name = to_string(action);
        ASSERT_FALSE(name.empty());
        const auto parsed = parse_action(name);
        ASSERT_TRUE(parsed.has_value()) << name;
        EXPECT_EQ(parsed.value(), action) << name;
    }
}

// --- binding -----------------------------------------------------------------

TEST(CommandMapTest, BindingReplacesWhatAChordDidBefore) {
    CommandMap map = CommandMap::defaults();
    const KeyChord b{Key::b, Modifiers::none};
    map.bind(b, Action::select_tool_slide);
    EXPECT_EQ(map.lookup(b), Action::select_tool_slide);
}

TEST(CommandMapTest, AnActionMayHaveSeveralChords) {
    CommandMap map = CommandMap::defaults();
    map.bind(KeyChord{Key::r, Modifiers::none}, Action::select_tool_ripple);
    const auto chords = map.chords_for(Action::select_tool_ripple);
    ASSERT_EQ(chords.size(), 2u);
    EXPECT_EQ(to_string(chords[0]), "B") << "sorted by spelling, not by hash order";
    EXPECT_EQ(to_string(chords[1]), "R");
}

TEST(CommandMapTest, UnbindingRemovesAChordAndReportsWhetherItDid) {
    CommandMap map = CommandMap::defaults();
    const KeyChord b{Key::b, Modifiers::none};
    EXPECT_TRUE(map.unbind(b));
    EXPECT_FALSE(map.lookup(b).has_value());
    EXPECT_FALSE(map.unbind(b)) << "unbinding twice is not a second removal";
}

// --- the file format ---------------------------------------------------------

TEST(CommandMapTest, ParsesOverTheDefaultsSoOneChangeIsOneLine) {
    const auto map = CommandMap::parse(
        "reelforge-keymap/1\n"
        "# a comment, and a blank line follow\n"
        "\n"
        "bind R select_tool_ripple\n");
    ASSERT_TRUE(map.has_value()) << map.error().to_string();
    EXPECT_EQ(bound_to(map.value(), "R"), Action::select_tool_ripple);
    EXPECT_EQ(bound_to(map.value(), "Ctrl+Right"), Action::trim_forward)
        << "a user who moves one key must not inherit every other key";
}

TEST(CommandMapTest, StandaloneParsingStartsFromNothing) {
    const auto map = CommandMap::parse_standalone(
        "reelforge-keymap/1\n"
        "bind R select_tool_ripple\n");
    ASSERT_TRUE(map.has_value()) << map.error().to_string();
    EXPECT_EQ(map.value().size(), 1u);
}

TEST(CommandMapTest, UnbindRemovesADefault) {
    const auto map = CommandMap::parse(
        "reelforge-keymap/1\n"
        "unbind B\n");
    ASSERT_TRUE(map.has_value()) << map.error().to_string();
    EXPECT_FALSE(map.value().lookup(parse_chord("B").value()).has_value());
}

TEST(CommandMapTest, RefusesAFileItDoesNotUnderstand) {
    EXPECT_EQ(CommandMap::parse("bind B select_tool_ripple\n").error().code(), Errc::corrupt_data)
        << "no header";
    EXPECT_EQ(CommandMap::parse("").error().code(), Errc::corrupt_data) << "empty";
    EXPECT_EQ(CommandMap::parse("reelforge-keymap/99\n").error().code(), Errc::version_mismatch);
    EXPECT_EQ(CommandMap::parse("reelforge-keymap/1\nwibble B x\n").error().code(),
              Errc::corrupt_data);
    EXPECT_TRUE(CommandMap::parse("reelforge-keymap/1\nbind Hyper+B select_tool_ripple\n")
                    .has_error());
    EXPECT_EQ(CommandMap::parse("reelforge-keymap/1\nbind B no_such_action\n").error().code(),
              Errc::not_found);
    EXPECT_TRUE(CommandMap::parse("reelforge-keymap/1\nbind B select_tool_ripple extra\n")
                    .has_error())
        << "trailing junk must not be ignored";
}

TEST(CommandMapTest, NamesTheLineThatIsWrong) {
    const auto bad = CommandMap::parse(
        "reelforge-keymap/1\n"
        "bind B select_tool_ripple\n"
        "bind Q no_such_action\n");
    ASSERT_TRUE(bad.has_error());
    EXPECT_NE(bad.error().to_string().find("line 3"), std::string::npos)
        << bad.error().to_string();
}

TEST(CommandMapTest, SerialisesCanonicallyAndReadsItselfBack) {
    CommandMap map = CommandMap::defaults();
    map.bind(KeyChord{Key::r, Modifiers::none}, Action::select_tool_ripple);

    const std::string text = map.serialise();
    EXPECT_EQ(text, map.serialise()) << "same map, same bytes";

    const auto reloaded = CommandMap::parse_standalone(text);
    ASSERT_TRUE(reloaded.has_value()) << reloaded.error().to_string();
    EXPECT_EQ(reloaded.value().serialise(), text);
}

TEST(CommandMapTest, ToleratesWindowsLineEndings) {
    // Keymaps get shared between machines; a CRLF must not be part of a key name.
    const auto map = CommandMap::parse("reelforge-keymap/1\r\nbind R select_tool_ripple\r\n");
    ASSERT_TRUE(map.has_value()) << map.error().to_string();
    EXPECT_EQ(bound_to(map.value(), "R"), Action::select_tool_ripple);
}

}  // namespace
