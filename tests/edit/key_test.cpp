#include "rf/edit/key.hpp"

#include <gtest/gtest.h>

#include <string>
#include <unordered_set>

namespace {

using rf::Errc;
using rf::edit::Key;
using rf::edit::KeyChord;
using rf::edit::Modifiers;
using rf::edit::parse_chord;
using rf::edit::to_string;

TEST(KeyChordTest, ParsesAPlainKey) {
    const auto chord = parse_chord("B");
    ASSERT_TRUE(chord.has_value()) << chord.error().to_string();
    EXPECT_EQ(chord.value().key, Key::b);
    EXPECT_EQ(chord.value().modifiers, Modifiers::none);
}

TEST(KeyChordTest, ParsesModifiersInAnyOrderAndAnyCase) {
    const KeyChord expected{Key::right, Modifiers::ctrl | Modifiers::shift};
    EXPECT_EQ(parse_chord("Ctrl+Shift+Right").value(), expected);
    EXPECT_EQ(parse_chord("Shift+Ctrl+Right").value(), expected);
    EXPECT_EQ(parse_chord("shift+CTRL+right").value(), expected);
    EXPECT_EQ(parse_chord("  Ctrl + Shift + Right  ").value(), expected);
}

TEST(KeyChordTest, FormatsOneChordExactlyOneWay) {
    // A file that could name the same binding two ways would let a later line
    // meant to override an earlier one quietly add a second binding instead.
    const KeyChord chord{Key::right, Modifiers::shift | Modifiers::ctrl};
    EXPECT_EQ(to_string(chord), "Ctrl+Shift+Right");
    EXPECT_EQ(to_string(parse_chord("Shift+Ctrl+Right").value()), "Ctrl+Shift+Right");
}

TEST(KeyChordTest, RoundTripsEveryModifierCombination) {
    for (const Modifiers modifiers :
         {Modifiers::none, Modifiers::shift, Modifiers::ctrl, Modifiers::alt,
          Modifiers::ctrl | Modifiers::shift, Modifiers::ctrl | Modifiers::alt,
          Modifiers::alt | Modifiers::shift,
          Modifiers::ctrl | Modifiers::alt | Modifiers::shift}) {
        const KeyChord chord{Key::f7, modifiers};
        const auto parsed = parse_chord(to_string(chord));
        ASSERT_TRUE(parsed.has_value()) << to_string(chord);
        EXPECT_EQ(parsed.value(), chord) << to_string(chord);
    }
}

TEST(KeyChordTest, RoundTripsEveryNamedKey) {
    // Catches a key that formats to a spelling the parser will not take back --
    // which would make a saved keymap unloadable for that one binding.
    for (std::uint16_t raw = 1; raw <= static_cast<std::uint16_t>(Key::f12); ++raw) {
        const Key key = static_cast<Key>(raw);
        const std::string_view name = to_string(key);
        ASSERT_FALSE(name.empty()) << "key " << raw << " has no name";
        const auto parsed = parse_chord(name);
        ASSERT_TRUE(parsed.has_value()) << name;
        EXPECT_EQ(parsed.value().key, key) << name;
    }
}

TEST(KeyChordTest, EveryNamedKeyHasADistinctName) {
    std::unordered_set<std::string> names;
    for (std::uint16_t raw = 1; raw <= static_cast<std::uint16_t>(Key::f12); ++raw) {
        const std::string name(to_string(static_cast<Key>(raw)));
        EXPECT_TRUE(names.insert(name).second) << "'" << name << "' names two keys";
    }
}

TEST(KeyChordTest, RejectsWhatItCannotName) {
    EXPECT_EQ(parse_chord("").error().code(), Errc::invalid_argument);
    EXPECT_EQ(parse_chord("   ").error().code(), Errc::invalid_argument);
    EXPECT_EQ(parse_chord("Hyper+A").error().code(), Errc::invalid_argument);
    EXPECT_EQ(parse_chord("Ctrl+").error().code(), Errc::invalid_argument);
    EXPECT_EQ(parse_chord("Ctrl+NoSuchKey").error().code(), Errc::invalid_argument);
}

TEST(KeyChordTest, NamesTheOffendingTextInTheError) {
    // The user is editing this file by hand; "invalid" alone would not help.
    const auto bad = parse_chord("Hyper+A");
    ASSERT_TRUE(bad.has_error());
    EXPECT_NE(bad.error().to_string().find("HYPER"), std::string::npos)
        << bad.error().to_string();
}

TEST(KeyChordTest, ChordsAreUsableAsHashKeys) {
    std::unordered_set<KeyChord> chords;
    EXPECT_TRUE(chords.insert(KeyChord{Key::a, Modifiers::none}).second);
    EXPECT_FALSE(chords.insert(KeyChord{Key::a, Modifiers::none}).second);
    EXPECT_TRUE(chords.insert(KeyChord{Key::a, Modifiers::ctrl}).second)
        << "a modifier makes a different chord";
}

}  // namespace
