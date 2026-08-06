#include "rf/edit/command_map.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

#include "rf/core/assert.hpp"

namespace rf::edit {
namespace {

struct DefaultBinding {
    std::string_view chord;
    Action action;
};

/// Premiere's defaults, read from Adobe's documentation rather than remembered.
/// The two that differ are marked; ADR 012 explains both.
constexpr DefaultBinding kDefaults[] = {
    {"V", Action::select_tool_selection},
    {"B", Action::select_tool_ripple},
    {"N", Action::select_tool_roll},
    {"Y", Action::select_tool_slip},
    {"U", Action::select_tool_slide},

    {"Ctrl+Left", Action::trim_backward},
    {"Ctrl+Right", Action::trim_forward},
    {"Ctrl+Shift+Left", Action::trim_backward_many},
    {"Ctrl+Shift+Right", Action::trim_forward_many},

    {"Up", Action::select_previous_clip},
    {"Down", Action::select_next_clip},
    {"Shift+Up", Action::select_previous_track},
    {"Shift+Down", Action::select_next_track},

    // No Premiere default. Premiere ships "Select Nearest Edit Point as Ripple
    // In/Out" unassigned, so a keyboard-only user cannot choose an edge out of
    // the box -- which M4's gate requires. ADR 012 decision 3.
    {"[", Action::select_in_edge},
    {"]", Action::select_out_edge},

    {"Ctrl+Z", Action::undo},
    {"Ctrl+Shift+Z", Action::redo},
};

[[nodiscard]] std::string_view strip(std::string_view text) {
    const auto is_space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    while (!text.empty() && is_space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_space(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

/// Splits off the first whitespace-delimited word, advancing `rest`.
[[nodiscard]] std::string_view take_word(std::string_view& rest) {
    rest = strip(rest);
    const std::size_t space = rest.find_first_of(" \t");
    if (space == std::string_view::npos) {
        const std::string_view word = rest;
        rest = {};
        return word;
    }
    const std::string_view word = rest.substr(0, space);
    rest.remove_prefix(space);
    return word;
}

[[nodiscard]] Result<CommandMap> parse_into(CommandMap map, std::string_view text) {
    std::size_t line_number = 0;
    bool saw_header = false;

    std::size_t position = 0;
    while (position <= text.size()) {
        const std::size_t newline = text.find('\n', position);
        const std::string_view raw =
            text.substr(position, newline == std::string_view::npos ? std::string_view::npos
                                                                    : newline - position);
        position = newline == std::string_view::npos ? text.size() + 1 : newline + 1;
        ++line_number;

        std::string_view line = strip(raw);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
            line = strip(line);
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }

        const std::string at = " at line " + std::to_string(line_number);

        if (!saw_header) {
            const std::string_view expected = "reelforge-keymap/";
            if (line.substr(0, expected.size()) != expected) {
                return Error{Errc::corrupt_data,
                             "keymap must begin with 'reelforge-keymap/<version>'" + at};
            }
            const std::string_view version = line.substr(expected.size());
            if (version != std::to_string(kKeymapFormatVersion)) {
                return Error{Errc::version_mismatch,
                             "keymap version '" + std::string(version) + "' is not version " +
                                 std::to_string(kKeymapFormatVersion) + at};
            }
            saw_header = true;
            continue;
        }

        std::string_view rest = line;
        const std::string_view verb = take_word(rest);
        if (verb != "bind" && verb != "unbind") {
            return Error{Errc::corrupt_data,
                         "expected 'bind' or 'unbind', got '" + std::string(verb) + "'" + at};
        }

        const std::string_view chord_text = take_word(rest);
        Result<KeyChord> chord = parse_chord(chord_text);
        if (!chord) {
            return chord.error().with_context("line " + std::to_string(line_number));
        }

        if (verb == "unbind") {
            if (!strip(rest).empty()) {
                return Error{Errc::corrupt_data, "unbind takes only a chord" + at};
            }
            map.unbind(chord.value());
            continue;
        }

        const std::string_view action_text = take_word(rest);
        if (!strip(rest).empty()) {
            return Error{Errc::corrupt_data,
                         "unexpected text after the action" + at};
        }
        Result<Action> action = parse_action(action_text);
        if (!action) {
            return action.error().with_context("line " + std::to_string(line_number));
        }
        map.bind(chord.value(), action.value());
    }

    if (!saw_header) {
        return Error{Errc::corrupt_data, "keymap is empty; expected a version header"};
    }
    return map;
}

}  // namespace

CommandMap CommandMap::defaults() {
    CommandMap map;
    for (const DefaultBinding& binding : kDefaults) {
        Result<KeyChord> chord = parse_chord(binding.chord);
        // The table above is a compile-time constant, so an unparseable entry is
        // a programming error, not anything a user can cause. Skipping it
        // quietly would ship a build with a key that silently does nothing.
        RF_CHECK_MSG(chord.has_value(), "a default key binding does not parse");
        map.bind(chord.value(), binding.action);
    }
    return map;
}

Result<CommandMap> CommandMap::parse(std::string_view text) {
    return parse_into(defaults(), text);
}

Result<CommandMap> CommandMap::parse_standalone(std::string_view text) {
    return parse_into(CommandMap{}, text);
}

std::optional<Action> CommandMap::lookup(const KeyChord& chord) const {
    const auto found = bindings_.find(chord);
    if (found == bindings_.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::vector<KeyChord> CommandMap::chords_for(Action action) const {
    std::vector<KeyChord> chords;
    for (const auto& [chord, bound] : bindings_) {
        if (bound == action) {
            chords.push_back(chord);
        }
    }
    // Sorted by spelling so the result does not depend on hash order, which
    // would make a UI listing shortcuts unstable between runs.
    std::sort(chords.begin(), chords.end(), [](const KeyChord& a, const KeyChord& b) {
        return to_string(a) < to_string(b);
    });
    return chords;
}

void CommandMap::bind(const KeyChord& chord, Action action) {
    bindings_[chord] = action;
}

bool CommandMap::unbind(const KeyChord& chord) {
    return bindings_.erase(chord) > 0;
}

std::string CommandMap::serialise() const {
    std::vector<std::pair<std::string, Action>> lines;
    lines.reserve(bindings_.size());
    for (const auto& [chord, action] : bindings_) {
        lines.emplace_back(to_string(chord), action);
    }
    std::sort(lines.begin(), lines.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::string out = "reelforge-keymap/" + std::to_string(kKeymapFormatVersion) + "\n";
    for (const auto& [chord, action] : lines) {
        out.append("bind ");
        out.append(chord);
        out.push_back(' ');
        out.append(to_string(action));
        out.push_back('\n');
    }
    return out;
}

}  // namespace rf::edit
