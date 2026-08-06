// Commands and the undo/redo stack.
//
// Every edit is a command that knows how to apply itself and how to undo itself
// exactly. See docs/adr/005-timeline-model-and-undo.md for why inversion rather
// than snapshots, and what that costs.
//
// Two invariants make undo trustworthy, and both are easy to get wrong:
//
//  1. A command that fails must leave the document untouched. Half-applied
//     edits cannot be undone, because the stack has no record of what happened.
//  2. Re-applying a command (redo) must reproduce the *same* result, including
//     the same ids. A redo that issues fresh ids produces a document that looks
//     right and no longer matches the one the user undid.

#ifndef RF_TIMELINE_COMMAND_HPP
#define RF_TIMELINE_COMMAND_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "rf/core/result.hpp"
#include "rf/timeline/document.hpp"

namespace rf::timeline {

class Command {
public:
    virtual ~Command() = default;

    Command(const Command&) = delete;
    Command& operator=(const Command&) = delete;

    /// Applies the edit. Called once when the user performs it, and again on
    /// every redo -- which must reproduce an identical document.
    [[nodiscard]] virtual Result<void> apply(Document& document) = 0;

    /// Reverses the most recent apply exactly, id counter included.
    [[nodiscard]] virtual Result<void> revert(Document& document) = 0;

    /// Short label for the undo menu, e.g. "Add Clip".
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

protected:
    Command() = default;
};

/// The undo/redo history.
///
/// Executing a new command discards the redo branch, which is what every editor
/// does and what users expect: once you edit after undoing, the abandoned
/// future is gone.
class CommandStack {
public:
    CommandStack() = default;
    CommandStack(const CommandStack&) = delete;
    CommandStack& operator=(const CommandStack&) = delete;
    CommandStack(CommandStack&&) noexcept = default;
    CommandStack& operator=(CommandStack&&) noexcept = default;
    ~CommandStack() = default;

    /// Applies `command` and, on success, pushes it onto the undo history.
    ///
    /// On failure the command is discarded and the history is untouched: a
    /// refused edit must not become an undo entry that would "undo" something
    /// that never happened.
    [[nodiscard]] Result<void> execute(Document& document, std::unique_ptr<Command> command);

    [[nodiscard]] Result<void> undo(Document& document);
    [[nodiscard]] Result<void> redo(Document& document);

    [[nodiscard]] bool can_undo() const noexcept { return !done_.empty(); }
    [[nodiscard]] bool can_redo() const noexcept { return !undone_.empty(); }
    [[nodiscard]] std::size_t undo_depth() const noexcept { return done_.size(); }
    [[nodiscard]] std::size_t redo_depth() const noexcept { return undone_.size(); }

    /// Label of the edit that undo would reverse, or empty.
    [[nodiscard]] std::string_view undo_name() const noexcept;
    [[nodiscard]] std::string_view redo_name() const noexcept;

    void clear() noexcept;

private:
    std::vector<std::unique_ptr<Command>> done_;
    std::vector<std::unique_ptr<Command>> undone_;
};

// --- concrete edits ----------------------------------------------------------

[[nodiscard]] std::unique_ptr<Command> make_add_track(TrackKind kind, std::string name);
[[nodiscard]] std::unique_ptr<Command> make_remove_track(TrackId id);
[[nodiscard]] std::unique_ptr<Command> make_add_clip(TrackId track, std::string source,
                                                     Ticks source_in, Ticks start, Ticks duration,
                                                     Ticks source_duration);
[[nodiscard]] std::unique_ptr<Command> make_remove_clip(ClipId id);
[[nodiscard]] std::unique_ptr<Command> make_move_clip(ClipId id, TrackId to_track, Ticks new_start);
[[nodiscard]] std::unique_ptr<Command> make_set_clip_bounds(ClipId id, Ticks source_in, Ticks start,
                                                            Ticks duration);
[[nodiscard]] std::unique_ptr<Command> make_set_clip_enabled(ClipId id, bool enabled);
[[nodiscard]] std::unique_ptr<Command> make_set_track_muted(TrackId id, bool muted);
[[nodiscard]] std::unique_ptr<Command> make_set_track_locked(TrackId id, bool locked);

}  // namespace rf::timeline

#endif  // RF_TIMELINE_COMMAND_HPP
