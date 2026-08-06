#include "rf/edit/key.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>
#include <vector>

namespace rf::edit {
namespace {

struct Named {
    Key key;
    std::string_view name;
};

/// One table serves both directions, so a key can never format as one spelling
/// and parse from another.
constexpr Named kKeyNames[] = {
    {Key::a, "A"}, {Key::b, "B"}, {Key::c, "C"}, {Key::d, "D"}, {Key::e, "E"},
    {Key::f, "F"}, {Key::g, "G"}, {Key::h, "H"}, {Key::i, "I"}, {Key::j, "J"},
    {Key::k, "K"}, {Key::l, "L"}, {Key::m, "M"}, {Key::n, "N"}, {Key::o, "O"},
    {Key::p, "P"}, {Key::q, "Q"}, {Key::r, "R"}, {Key::s, "S"}, {Key::t, "T"},
    {Key::u, "U"}, {Key::v, "V"}, {Key::w, "W"}, {Key::x, "X"}, {Key::y, "Y"},
    {Key::z, "Z"},

    {Key::digit_0, "0"}, {Key::digit_1, "1"}, {Key::digit_2, "2"}, {Key::digit_3, "3"},
    {Key::digit_4, "4"}, {Key::digit_5, "5"}, {Key::digit_6, "6"}, {Key::digit_7, "7"},
    {Key::digit_8, "8"}, {Key::digit_9, "9"},

    {Key::left, "Left"}, {Key::right, "Right"}, {Key::up, "Up"}, {Key::down, "Down"},
    {Key::home, "Home"}, {Key::end, "End"}, {Key::page_up, "PageUp"},
    {Key::page_down, "PageDown"},

    {Key::space, "Space"}, {Key::enter, "Enter"}, {Key::escape, "Escape"},
    {Key::tab, "Tab"}, {Key::backspace, "Backspace"}, {Key::del, "Delete"},
    {Key::left_bracket, "["}, {Key::right_bracket, "]"}, {Key::comma, ","},
    {Key::period, "."}, {Key::semicolon, ";"}, {Key::minus, "-"}, {Key::equal, "="},

    {Key::f1, "F1"}, {Key::f2, "F2"}, {Key::f3, "F3"}, {Key::f4, "F4"},
    {Key::f5, "F5"}, {Key::f6, "F6"}, {Key::f7, "F7"}, {Key::f8, "F8"},
    {Key::f9, "F9"}, {Key::f10, "F10"}, {Key::f11, "F11"}, {Key::f12, "F12"},
};

[[nodiscard]] std::string upper(std::string_view text) {
    std::string out(text);
    for (char& character : out) {
        character = static_cast<char>(
            std::toupper(static_cast<unsigned char>(character)));
    }
    return out;
}

[[nodiscard]] std::string trim(std::string_view text) {
    const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    std::size_t begin = 0;
    while (begin < text.size() && is_space(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && is_space(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

}  // namespace

std::string_view to_string(Key key) noexcept {
    for (const Named& named : kKeyNames) {
        if (named.key == key && !named.name.empty()) {
            return named.name;
        }
    }
    return "";
}

std::string to_string(const KeyChord& chord) {
    std::string out;
    // Fixed order, so one chord has exactly one spelling. Without that, a keymap
    // could name the same binding two ways and a later line meant to override an
    // earlier one would quietly add a second binding instead.
    if (has(chord.modifiers, Modifiers::ctrl)) {
        out.append("Ctrl+");
    }
    if (has(chord.modifiers, Modifiers::alt)) {
        out.append("Alt+");
    }
    if (has(chord.modifiers, Modifiers::shift)) {
        out.append("Shift+");
    }
    out.append(to_string(chord.key));
    return out;
}

Result<KeyChord> parse_chord(std::string_view text) {
    const std::string cleaned = trim(text);
    if (cleaned.empty()) {
        return Error{Errc::invalid_argument, "empty key chord"};
    }

    KeyChord chord;
    std::size_t position = 0;
    while (true) {
        const std::size_t plus = cleaned.find('+', position);
        // A trailing '+' is the key itself only if it is the whole remainder;
        // ReelForge has no '+' key, so this is always a separator.
        if (plus == std::string::npos) {
            break;
        }
        const std::string modifier = upper(trim(cleaned.substr(position, plus - position)));
        if (modifier == "CTRL" || modifier == "CONTROL") {
            chord.modifiers = chord.modifiers | Modifiers::ctrl;
        } else if (modifier == "SHIFT") {
            chord.modifiers = chord.modifiers | Modifiers::shift;
        } else if (modifier == "ALT" || modifier == "OPTION") {
            chord.modifiers = chord.modifiers | Modifiers::alt;
        } else {
            return Error{Errc::invalid_argument,
                         "unknown modifier '" + modifier + "' in chord '" + cleaned + "'"};
        }
        position = plus + 1;
    }

    const std::string name = upper(trim(cleaned.substr(position)));
    if (name.empty()) {
        return Error{Errc::invalid_argument, "chord '" + cleaned + "' names no key"};
    }
    for (const Named& named : kKeyNames) {
        if (!named.name.empty() && upper(named.name) == name) {
            chord.key = named.key;
            return chord;
        }
    }
    return Error{Errc::invalid_argument, "unknown key '" + name + "' in chord '" + cleaned + "'"};
}

}  // namespace rf::edit
