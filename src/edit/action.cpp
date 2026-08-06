#include "rf/edit/action.hpp"

#include <string>

namespace rf::edit {
namespace {

struct NamedAction {
    Action action;
    std::string_view name;
};

/// One table both directions, so an action cannot write itself under a name the
/// parser will not accept -- which would make a saved keymap unloadable.
constexpr NamedAction kActionNames[] = {
    {Action::select_tool_selection, "select_tool_selection"},
    {Action::select_tool_ripple, "select_tool_ripple"},
    {Action::select_tool_roll, "select_tool_roll"},
    {Action::select_tool_slip, "select_tool_slip"},
    {Action::select_tool_slide, "select_tool_slide"},

    {Action::trim_backward, "trim_backward"},
    {Action::trim_forward, "trim_forward"},
    {Action::trim_backward_many, "trim_backward_many"},
    {Action::trim_forward_many, "trim_forward_many"},

    {Action::select_previous_clip, "select_previous_clip"},
    {Action::select_next_clip, "select_next_clip"},
    {Action::select_previous_track, "select_previous_track"},
    {Action::select_next_track, "select_next_track"},

    {Action::select_in_edge, "select_in_edge"},
    {Action::select_out_edge, "select_out_edge"},

    {Action::shuttle_forward, "shuttle_forward"},
    {Action::shuttle_backward, "shuttle_backward"},
    {Action::shuttle_stop, "shuttle_stop"},
    {Action::shuttle_slow_forward, "shuttle_slow_forward"},
    {Action::shuttle_slow_backward, "shuttle_slow_backward"},

    {Action::undo, "undo"},
    {Action::redo, "redo"},
};

}  // namespace

std::string_view to_string(Tool tool) noexcept {
    switch (tool) {
        case Tool::selection: return "Selection";
        case Tool::ripple:    return "Ripple Edit";
        case Tool::roll:      return "Rolling Edit";
        case Tool::slip:      return "Slip";
        case Tool::slide:     return "Slide";
    }
    return "Selection";
}

std::string_view to_string(Edge edge) noexcept {
    return edge == Edge::in ? "in" : "out";
}

std::string_view to_string(Action action) noexcept {
    for (const NamedAction& named : kActionNames) {
        if (named.action == action) {
            return named.name;
        }
    }
    return "";
}

Result<Action> parse_action(std::string_view name) {
    for (const NamedAction& named : kActionNames) {
        if (named.name == name) {
            return named.action;
        }
    }
    return Error{Errc::not_found, "no action named '" + std::string(name) + "'"};
}

}  // namespace rf::edit
