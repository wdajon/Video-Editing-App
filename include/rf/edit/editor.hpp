// The state a keystroke acts on, and the thing that acts.
//
// Premiere's trim is modal: pick a tool, select an edit point, press a trim key.
// `EditState` is exactly that state, and `Editor` turns an `Action` into a
// command on the document. Neither knows about Qt, so the whole keyboard
// workflow is testable without a window.

#ifndef RF_EDIT_EDITOR_HPP
#define RF_EDIT_EDITOR_HPP

#include "rf/core/result.hpp"
#include "rf/edit/action.hpp"
#include "rf/edit/command_map.hpp"
#include "rf/edit/key.hpp"
#include "rf/timeline/command.hpp"
#include "rf/timeline/document.hpp"
#include "rf/timeline/trim.hpp"

namespace rf::edit {

/// What the next keystroke will act on.
struct EditState {
    Tool tool = Tool::selection;
    timeline::TrackId track;  ///< Null until something is selected.
    timeline::ClipId clip;    ///< Null until something is selected.
    Edge edge = Edge::out;    ///< Which end of `clip` a ripple or roll moves.

    // There is no playhead here yet. Nothing in this layer reads one: the trim
    // keys work from the selection, and the actions that need a playhead --
    // Premiere's Q and W, ripple trim to the playhead -- are not implemented. A
    // field written by selection and read by nothing would look like state and
    // behave like decoration.

    /// Frames a "many" trim moves. Premiere's default is 5, and it is a user
    /// preference there too -- so it lives here rather than as a literal in the
    /// trim path.
    int large_trim_frames = 5;
};

/// Applies actions to a document.
///
/// Holds no state of its own: the document, the undo stack and the edit state
/// are all the caller's, because the Qt layer owns them and this has to be
/// constructible around them in a test with no window.
class Editor {
public:
    Editor(timeline::Document& document, timeline::CommandStack& stack, EditState& state) noexcept
        : document_(document), stack_(stack), state_(state) {}

    /// Performs `action`.
    ///
    /// An action that does not apply -- a trim with the selection tool active,
    /// or with nothing selected -- returns an error naming what is missing
    /// rather than doing nothing. Silence is indistinguishable from a broken key
    /// binding, and a keyboard-only user has nothing else to go on.
    [[nodiscard]] Result<void> perform(Action action);

    /// Looks `chord` up in `map` and performs whatever it is bound to.
    /// An unbound chord is `Errc::not_found`, which a UI may reasonably ignore.
    [[nodiscard]] Result<void> press(const CommandMap& map, const KeyChord& chord);

    /// The trim `state.tool` and `state.edge` currently mean, or an error when
    /// the tool does not trim. Public because the UI needs it to decide what to
    /// draw before anything is pressed.
    [[nodiscard]] static Result<timeline::TrimKind> trim_kind(Tool tool, Edge edge);

private:
    [[nodiscard]] Result<void> apply_trim(int frames);
    [[nodiscard]] Result<void> select_clip_by_offset(int offset);
    [[nodiscard]] Result<void> select_track_by_offset(int offset);
    /// Selects the first clip of `track`, or clears the clip if it has none.
    void select_first_clip_of(timeline::TrackId track);

    timeline::Document& document_;
    timeline::CommandStack& stack_;
    EditState& state_;
};

}  // namespace rf::edit

#endif  // RF_EDIT_EDITOR_HPP
