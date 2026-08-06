// Naming a keystroke, without Qt.
//
// `rf_edit` is the interaction model -- tools, selection, key bindings -- and it
// is deliberately free of any toolkit, which is what lets the whole keyboard
// workflow be a headless test rather than a screenshot. Qt translates its own
// key events into these at the edge and no further.

#ifndef RF_EDIT_KEY_HPP
#define RF_EDIT_KEY_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "rf/core/result.hpp"

namespace rf::edit {

/// Keys ReelForge can bind. Deliberately not every key a keyboard has: an
/// enumeration that claims to cover a keyboard it has never been tested against
/// is a promise the parser cannot keep.
enum class Key : std::uint16_t {
    none = 0,

    a, b, c, d, e, f, g, h, i, j, k, l, m,
    n, o, p, q, r, s, t, u, v, w, x, y, z,

    digit_0, digit_1, digit_2, digit_3, digit_4,
    digit_5, digit_6, digit_7, digit_8, digit_9,

    left, right, up, down,
    home, end, page_up, page_down,

    space, enter, escape, tab, backspace, del,
    left_bracket, right_bracket, comma, period, semicolon, minus, equal,

    f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12,
};

/// Modifier keys, as a mask.
///
/// There is no Command/Meta here yet. macOS is not a supported platform (it is
/// in the backlog), and adding a modifier that nothing can produce would be a
/// binding nobody could press.
enum class Modifiers : std::uint8_t {
    none = 0,
    shift = 1 << 0,
    ctrl = 1 << 1,
    alt = 1 << 2,
};

[[nodiscard]] constexpr Modifiers operator|(Modifiers lhs, Modifiers rhs) noexcept {
    return static_cast<Modifiers>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}
[[nodiscard]] constexpr bool has(Modifiers set, Modifiers one) noexcept {
    return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(one)) != 0;
}

/// One keystroke: a key and the modifiers held with it.
struct KeyChord {
    Key key = Key::none;
    Modifiers modifiers = Modifiers::none;

    friend bool operator==(const KeyChord&, const KeyChord&) = default;
};

/// Canonical spelling, e.g. "Ctrl+Shift+Right". Modifiers always appear in the
/// order Ctrl, Alt, Shift so one chord has exactly one spelling -- a keymap file
/// that could name the same binding two ways would let a later line silently
/// fail to override an earlier one.
[[nodiscard]] std::string to_string(const KeyChord& chord);
[[nodiscard]] std::string_view to_string(Key key) noexcept;

/// Parses a chord. Accepts modifiers in any order and is case-insensitive, since
/// this reads a file people write by hand.
[[nodiscard]] Result<KeyChord> parse_chord(std::string_view text);

}  // namespace rf::edit

namespace std {
template <>
struct hash<rf::edit::KeyChord> {
    std::size_t operator()(const rf::edit::KeyChord& chord) const noexcept {
        return (static_cast<std::size_t>(chord.key) << 8) |
               static_cast<std::size_t>(chord.modifiers);
    }
};
}  // namespace std

#endif  // RF_EDIT_KEY_HPP
