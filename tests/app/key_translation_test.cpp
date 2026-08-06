#include "rf/app/key_translation.hpp"

#include <gtest/gtest.h>

#include <QKeyEvent>

namespace {

using rf::app::to_key_chord;
using rf::app::to_qt_key;
using rf::edit::Key;
using rf::edit::KeyChord;
using rf::edit::Modifiers;

QKeyEvent press(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    return QKeyEvent(QEvent::KeyPress, key, modifiers);
}

TEST(KeyTranslation, EveryKeyReelForgeCanNameSurvivesTheRoundTrip) {
    // A key that translates one way and not back is a binding a user can write
    // in a keymap and never trigger.
    for (std::uint16_t raw = 1; raw <= static_cast<std::uint16_t>(Key::f12); ++raw) {
        const Key key = static_cast<Key>(raw);
        const int qt_key = to_qt_key(key);
        ASSERT_NE(qt_key, Qt::Key_unknown) << "key " << raw << " has no Qt code";

        const QKeyEvent event = press(qt_key);
        const auto chord = to_key_chord(event);
        ASSERT_TRUE(chord.has_value()) << "key " << raw << " does not translate back";
        EXPECT_EQ(chord->key, key) << "key " << raw;
    }
}

TEST(KeyTranslation, CarriesTheThreeModifiersReelForgeCanExpress) {
    const QKeyEvent event = press(Qt::Key_Right, Qt::ControlModifier | Qt::ShiftModifier);
    const auto chord = to_key_chord(event);
    ASSERT_TRUE(chord.has_value());
    EXPECT_EQ(chord.value(), (KeyChord{Key::right, Modifiers::ctrl | Modifiers::shift}));
}

TEST(KeyTranslation, MasksTheKeypadModifier) {
    // The same keystroke as far as the user is concerned. Left unmasked, a trim
    // from the numeric keypad would be a different chord and would do nothing.
    const auto from_keypad =
        to_key_chord(press(Qt::Key_Right, Qt::ControlModifier | Qt::KeypadModifier));
    const auto from_cluster = to_key_chord(press(Qt::Key_Right, Qt::ControlModifier));
    ASSERT_TRUE(from_keypad.has_value());
    ASSERT_TRUE(from_cluster.has_value());
    EXPECT_EQ(from_keypad.value(), from_cluster.value());
}

TEST(KeyTranslation, IgnoresModifiersItCannotExpress) {
    // Meta exists on the keyboard and not in `Modifiers`. Dropping it is the
    // documented behaviour; inventing a chord for it would bind Meta+B to B.
    const auto chord = to_key_chord(press(Qt::Key_B, Qt::MetaModifier));
    ASSERT_TRUE(chord.has_value());
    EXPECT_EQ(chord.value(), (KeyChord{Key::b, Modifiers::none}));
}

TEST(KeyTranslation, ReturnsNothingForAKeyItCannotName) {
    // Falling through to some default would bind a key to an action the user
    // never asked for, which is worse than the key doing nothing.
    EXPECT_FALSE(to_key_chord(press(Qt::Key_Meta)).has_value());
    EXPECT_FALSE(to_key_chord(press(Qt::Key_MediaPlay)).has_value());
    EXPECT_FALSE(to_key_chord(press(Qt::Key_unknown)).has_value());
}

TEST(KeyTranslation, MapsTheBracketsTheEdgeBindingsUse) {
    // ADR 013's two non-Premiere bindings. If these did not translate, the gate's
    // workflow would have no way to choose an edge from a real keyboard.
    EXPECT_EQ(to_key_chord(press(Qt::Key_BracketLeft))->key, Key::left_bracket);
    EXPECT_EQ(to_key_chord(press(Qt::Key_BracketRight))->key, Key::right_bracket);
}

}  // namespace
