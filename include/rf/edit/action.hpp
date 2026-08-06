// What a key does, named independently of which key it is.
//
// The separation is the whole point of a remappable command map: a binding is a
// pair of (chord, action), and neither half knows about the other.

#ifndef RF_EDIT_ACTION_HPP
#define RF_EDIT_ACTION_HPP

#include <cstdint>
#include <string_view>

#include "rf/core/result.hpp"

namespace rf::edit {

/// The editing tool a trim key applies. Premiere's model, and its letters.
enum class Tool : std::uint8_t {
    selection = 0,  ///< V. Moves and selects; trim keys do not apply.
    ripple = 1,     ///< B.
    roll = 2,       ///< N.
    slip = 3,       ///< Y.
    slide = 4,      ///< U.
};

[[nodiscard]] std::string_view to_string(Tool tool) noexcept;

/// Which end of the selected clip a ripple or roll acts on.
enum class Edge : std::uint8_t {
    in = 0,
    out = 1,
};

[[nodiscard]] std::string_view to_string(Edge edge) noexcept;

/// Everything a key can be bound to.
enum class Action : std::uint16_t {
    select_tool_selection,
    select_tool_ripple,
    select_tool_roll,
    select_tool_slip,
    select_tool_slide,

    trim_backward,       ///< One frame.
    trim_forward,        ///< One frame.
    trim_backward_many,  ///< By the large trim offset.
    trim_forward_many,

    select_previous_clip,
    select_next_clip,
    select_previous_track,
    select_next_track,

    select_in_edge,
    select_out_edge,

    undo,
    redo,
};

/// Stable, machine-greppable spelling, as written in a keymap file.
[[nodiscard]] std::string_view to_string(Action action) noexcept;
[[nodiscard]] Result<Action> parse_action(std::string_view name);

}  // namespace rf::edit

#endif  // RF_EDIT_ACTION_HPP
