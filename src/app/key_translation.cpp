#include "rf/app/key_translation.hpp"

namespace rf::app {
namespace {

using edit::Key;

struct KeyPair {
    Key key;
    int qt_key;
};

/// One table, both directions, so a key cannot translate one way and not back.
/// A test walks the whole `Key` enumeration through it.
constexpr KeyPair kKeys[] = {
    {Key::a, Qt::Key_A}, {Key::b, Qt::Key_B}, {Key::c, Qt::Key_C}, {Key::d, Qt::Key_D},
    {Key::e, Qt::Key_E}, {Key::f, Qt::Key_F}, {Key::g, Qt::Key_G}, {Key::h, Qt::Key_H},
    {Key::i, Qt::Key_I}, {Key::j, Qt::Key_J}, {Key::k, Qt::Key_K}, {Key::l, Qt::Key_L},
    {Key::m, Qt::Key_M}, {Key::n, Qt::Key_N}, {Key::o, Qt::Key_O}, {Key::p, Qt::Key_P},
    {Key::q, Qt::Key_Q}, {Key::r, Qt::Key_R}, {Key::s, Qt::Key_S}, {Key::t, Qt::Key_T},
    {Key::u, Qt::Key_U}, {Key::v, Qt::Key_V}, {Key::w, Qt::Key_W}, {Key::x, Qt::Key_X},
    {Key::y, Qt::Key_Y}, {Key::z, Qt::Key_Z},

    {Key::digit_0, Qt::Key_0}, {Key::digit_1, Qt::Key_1}, {Key::digit_2, Qt::Key_2},
    {Key::digit_3, Qt::Key_3}, {Key::digit_4, Qt::Key_4}, {Key::digit_5, Qt::Key_5},
    {Key::digit_6, Qt::Key_6}, {Key::digit_7, Qt::Key_7}, {Key::digit_8, Qt::Key_8},
    {Key::digit_9, Qt::Key_9},

    {Key::left, Qt::Key_Left}, {Key::right, Qt::Key_Right}, {Key::up, Qt::Key_Up},
    {Key::down, Qt::Key_Down}, {Key::home, Qt::Key_Home}, {Key::end, Qt::Key_End},
    {Key::page_up, Qt::Key_PageUp}, {Key::page_down, Qt::Key_PageDown},

    {Key::space, Qt::Key_Space}, {Key::enter, Qt::Key_Return}, {Key::escape, Qt::Key_Escape},
    {Key::tab, Qt::Key_Tab}, {Key::backspace, Qt::Key_Backspace}, {Key::del, Qt::Key_Delete},
    {Key::left_bracket, Qt::Key_BracketLeft}, {Key::right_bracket, Qt::Key_BracketRight},
    {Key::comma, Qt::Key_Comma}, {Key::period, Qt::Key_Period},
    {Key::semicolon, Qt::Key_Semicolon}, {Key::minus, Qt::Key_Minus},
    {Key::equal, Qt::Key_Equal},

    {Key::f1, Qt::Key_F1}, {Key::f2, Qt::Key_F2}, {Key::f3, Qt::Key_F3},
    {Key::f4, Qt::Key_F4}, {Key::f5, Qt::Key_F5}, {Key::f6, Qt::Key_F6},
    {Key::f7, Qt::Key_F7}, {Key::f8, Qt::Key_F8}, {Key::f9, Qt::Key_F9},
    {Key::f10, Qt::Key_F10}, {Key::f11, Qt::Key_F11}, {Key::f12, Qt::Key_F12},
};

}  // namespace

int to_qt_key(edit::Key key) noexcept {
    for (const KeyPair& pair : kKeys) {
        if (pair.key == key) {
            return pair.qt_key;
        }
    }
    return Qt::Key_unknown;
}

std::optional<edit::KeyChord> to_key_chord(const QKeyEvent& event) {
    edit::KeyChord chord;

    bool found = false;
    for (const KeyPair& pair : kKeys) {
        if (pair.qt_key == event.key()) {
            chord.key = pair.key;
            found = true;
            break;
        }
    }
    if (!found) {
        return std::nullopt;
    }

    // Only the three modifiers `Modifiers` can express survive. Qt also reports
    // KeypadModifier for the numeric keypad, which would otherwise make
    // Ctrl+Right from the keypad a different chord from Ctrl+Right from the
    // arrow cluster -- the same keystroke as far as the user is concerned.
    const Qt::KeyboardModifiers modifiers = event.modifiers();
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        chord.modifiers = chord.modifiers | edit::Modifiers::shift;
    }
    if (modifiers.testFlag(Qt::ControlModifier)) {
        chord.modifiers = chord.modifiers | edit::Modifiers::ctrl;
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        chord.modifiers = chord.modifiers | edit::Modifiers::alt;
    }
    return chord;
}

}  // namespace rf::app
