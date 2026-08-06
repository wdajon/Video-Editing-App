#include "rf/edit/editor.hpp"

#include <string>
#include <vector>

#include "rf/timeline/trim.hpp"

namespace rf::edit {
namespace {

using timeline::Clip;
using timeline::Track;
using timeline::TrimKind;

/// Index of `track` among the document's tracks, or npos.
[[nodiscard]] std::size_t index_of_track(const timeline::Document& document,
                                         timeline::TrackId id) {
    const std::vector<Track>& tracks = document.tracks();
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        if (tracks[i].id == id) {
            return i;
        }
    }
    return static_cast<std::size_t>(-1);
}

}  // namespace

Result<TrimKind> Editor::trim_kind(Tool tool, Edge edge) {
    switch (tool) {
        case Tool::ripple:
            return edge == Edge::in ? TrimKind::ripple_in : TrimKind::ripple_out;
        case Tool::roll:
            return TrimKind::roll;
        case Tool::slip:
            return TrimKind::slip;
        case Tool::slide:
            return TrimKind::slide;
        case Tool::selection:
            break;
    }
    return Error{Errc::invalid_argument,
                 "the " + std::string(to_string(tool)) +
                     " tool does not trim; press B, N, Y or U first"};
}

Result<void> Editor::apply_trim(int frames) {
    if (!state_.clip.is_valid()) {
        return Error{Errc::not_found, "nothing is selected to trim"};
    }
    Result<TrimKind> kind = trim_kind(state_.tool, state_.edge);
    if (!kind) {
        return kind.error();
    }
    if (document_.find_clip(state_.clip) == nullptr) {
        return Error{Errc::not_found,
                     to_string(state_.clip) + " is selected but no longer exists"};
    }

    // Frames to ticks is exact: Document refuses a frame rate whose period is
    // not a whole number of ticks, so a keyboard trim always lands on a frame
    // boundary. See docs/adr/012-command-map.md.
    const timeline::Ticks delta =
        static_cast<timeline::Ticks>(frames) * document_.ticks_per_frame();
    return stack_.execute(document_, timeline::make_trim(state_.clip, kind.value(), delta));
}

void Editor::select_first_clip_of(timeline::TrackId track) {
    state_.track = track;
    const Track* found = document_.find_track(track);
    state_.clip = (found == nullptr || found->clips.empty()) ? timeline::ClipId{}
                                                             : found->clips.front().id;
}

Result<void> Editor::select_clip_by_offset(int offset) {
    const Track* track = document_.find_track(state_.track);
    if (track == nullptr) {
        return Error{Errc::not_found, "no track is selected"};
    }
    if (track->clips.empty()) {
        return Error{Errc::not_found, track->name + " has no clips to select"};
    }

    // With nothing selected, either direction lands on the first clip: the user
    // has pressed a selection key on a track that has clips, and refusing would
    // leave them with no way to start from the keyboard.
    std::size_t index = 0;
    if (state_.clip.is_valid()) {
        for (std::size_t i = 0; i < track->clips.size(); ++i) {
            if (track->clips[i].id == state_.clip) {
                index = i;
                break;
            }
        }
        // Refusing at the ends rather than wrapping: a selection that silently
        // jumped from the last clip to the first would move an edit somewhere
        // the user was not looking.
        if (offset < 0 && index == 0) {
            return Error{Errc::not_found, "already at the first clip on this track"};
        }
        if (offset > 0 && index + 1 >= track->clips.size()) {
            return Error{Errc::not_found, "already at the last clip on this track"};
        }
        index = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(index) + offset);
    }

    state_.clip = track->clips[index].id;
    return ok();
}

Result<void> Editor::select_track_by_offset(int offset) {
    const std::vector<Track>& tracks = document_.tracks();
    if (tracks.empty()) {
        return Error{Errc::not_found, "the document has no tracks"};
    }
    if (!state_.track.is_valid()) {
        select_first_clip_of(tracks.front().id);
        return ok();
    }

    const std::size_t index = index_of_track(document_, state_.track);
    if (index == static_cast<std::size_t>(-1)) {
        return Error{Errc::not_found, "the selected track no longer exists"};
    }
    if (offset < 0 && index == 0) {
        return Error{Errc::not_found, "already on the first track"};
    }
    if (offset > 0 && index + 1 >= tracks.size()) {
        return Error{Errc::not_found, "already on the last track"};
    }

    select_first_clip_of(tracks[static_cast<std::size_t>(
                                    static_cast<std::ptrdiff_t>(index) + offset)]
                             .id);
    return ok();
}

Result<void> Editor::perform(Action action) {
    switch (action) {
        case Action::select_tool_selection: state_.tool = Tool::selection; return ok();
        case Action::select_tool_ripple:    state_.tool = Tool::ripple;    return ok();
        case Action::select_tool_roll:      state_.tool = Tool::roll;      return ok();
        case Action::select_tool_slip:      state_.tool = Tool::slip;      return ok();
        case Action::select_tool_slide:     state_.tool = Tool::slide;     return ok();

        case Action::trim_backward:      return apply_trim(-1);
        case Action::trim_forward:       return apply_trim(1);
        case Action::trim_backward_many: return apply_trim(-state_.large_trim_frames);
        case Action::trim_forward_many:  return apply_trim(state_.large_trim_frames);

        case Action::select_previous_clip:  return select_clip_by_offset(-1);
        case Action::select_next_clip:      return select_clip_by_offset(1);
        case Action::select_previous_track: return select_track_by_offset(-1);
        case Action::select_next_track:     return select_track_by_offset(1);

        case Action::select_in_edge:  state_.edge = Edge::in;  return ok();
        case Action::select_out_edge: state_.edge = Edge::out; return ok();

        case Action::shuttle_forward:       state_.shuttle.forward();       return ok();
        case Action::shuttle_backward:      state_.shuttle.backward();      return ok();
        case Action::shuttle_stop:          state_.shuttle.stop();          return ok();
        case Action::shuttle_slow_forward:  state_.shuttle.slow_forward();  return ok();
        case Action::shuttle_slow_backward: state_.shuttle.slow_backward(); return ok();

        case Action::undo: return stack_.undo(document_);
        case Action::redo: return stack_.redo(document_);
    }
    return Error{Errc::internal, "unknown action"};
}

Result<void> Editor::press(const CommandMap& map, const KeyChord& chord) {
    const std::optional<Action> action = map.lookup(chord);
    if (!action) {
        return Error{Errc::not_found, to_string(chord) + " is not bound to anything"};
    }
    return perform(*action);
}

}  // namespace rf::edit
