#include "rf/timeline/command.hpp"

#include <algorithm>
#include <utility>

namespace rf::timeline {
namespace {

/// Base for the two commands that create an object and therefore spend an id.
///
/// The id counter is document state, so an inverse that does not give the id
/// back leaves the document byte-different from where it started even though
/// every visible object matches. And a redo must reuse the id it issued the
/// first time, not mint a fresh one, or undo-then-redo silently produces a
/// different document. Both rules live here so no individual command can forget
/// one of them.
class CreatingCommand : public Command {
protected:
    /// Performs the creation for the first time and returns the new id.
    [[nodiscard]] virtual Result<void> create_first_time(Document& document) = 0;
    /// Recreates the object with the identity captured on the first apply.
    [[nodiscard]] virtual Result<void> recreate(Document& document) = 0;
    /// Removes what was created.
    [[nodiscard]] virtual Result<void> destroy(Document& document) = 0;

    [[nodiscard]] Result<void> apply(Document& document) final {
        if (!created_once_) {
            const std::uint64_t before = document.next_id();
            if (Result<void> made = create_first_time(document); !made) {
                return made.error();
            }
            next_id_before_ = before;
            next_id_after_ = document.next_id();
            created_once_ = true;
            return ok();
        }

        if (Result<void> made = recreate(document); !made) {
            return made.error();
        }
        // Recreation inserts a known id and does not spend a new one, so the
        // counter has to be put back where the original apply left it.
        document.rewind_next_id(next_id_after_);
        return ok();
    }

    [[nodiscard]] Result<void> revert(Document& document) final {
        if (Result<void> gone = destroy(document); !gone) {
            return gone.error();
        }
        document.rewind_next_id(next_id_before_);
        return ok();
    }

private:
    std::uint64_t next_id_before_ = 0;
    std::uint64_t next_id_after_ = 0;
    bool created_once_ = false;
};

class AddTrackCommand final : public CreatingCommand {
public:
    AddTrackCommand(TrackKind kind, std::string name) : name_(std::move(name)), kind_(kind) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Add Track"; }

protected:
    [[nodiscard]] Result<void> create_first_time(Document& document) override {
        Result<TrackId> id = document.add_track(kind_, name_);
        if (!id) {
            return id.error();
        }
        id_ = id.value();
        index_ = document.tracks().size() - 1;
        return ok();
    }

    [[nodiscard]] Result<void> recreate(Document& document) override {
        Track track;
        track.id = id_;
        track.kind = kind_;
        track.name = name_;
        return document.insert_track_at(index_, std::move(track));
    }

    [[nodiscard]] Result<void> destroy(Document& document) override {
        Result<Track> removed = document.remove_track(id_);
        if (!removed) {
            return removed.error();
        }
        return ok();
    }

private:
    std::string name_;
    TrackKind kind_;
    TrackId id_;
    std::size_t index_ = 0;
};

class AddClipCommand final : public CreatingCommand {
public:
    AddClipCommand(TrackId track, std::string source, Ticks source_in, Ticks start, Ticks duration,
                   Ticks source_duration)
        : source_(std::move(source)),
          track_(track),
          source_in_(source_in),
          start_(start),
          duration_(duration),
          source_duration_(source_duration) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Add Clip"; }

protected:
    [[nodiscard]] Result<void> create_first_time(Document& document) override {
        Result<ClipId> id =
            document.add_clip(track_, source_, source_in_, start_, duration_, source_duration_);
        if (!id) {
            return id.error();
        }
        created_ = *document.find_clip(id.value());
        return ok();
    }

    [[nodiscard]] Result<void> recreate(Document& document) override {
        return document.insert_clip(track_, created_);
    }

    [[nodiscard]] Result<void> destroy(Document& document) override {
        Result<Clip> removed = document.remove_clip(created_.id);
        if (!removed) {
            return removed.error();
        }
        return ok();
    }

private:
    std::string source_;
    Clip created_;
    TrackId track_;
    Ticks source_in_;
    Ticks start_;
    Ticks duration_;
    Ticks source_duration_;
};

/// Linking spends an id, so it goes through `CreatingCommand` for the same
/// reason `AddClip` does: a redo must reuse the id it issued the first time, and
/// an undo must give the counter back.
class LinkClipsCommand final : public CreatingCommand {
public:
    explicit LinkClipsCommand(std::vector<ClipId> clips) : clips_(std::move(clips)) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Link Clips"; }

protected:
    [[nodiscard]] Result<void> create_first_time(Document& document) override {
        Result<LinkId> link = document.link_clips(clips_);
        if (!link) {
            return link.error();
        }
        link_ = link.value();
        return ok();
    }

    [[nodiscard]] Result<void> recreate(Document& document) override {
        return document.relink(link_, clips_);
    }

    [[nodiscard]] Result<void> destroy(Document& document) override {
        return document.unlink(link_);
    }

private:
    std::vector<ClipId> clips_;
    LinkId link_;
};

/// Unlinking records who was in the group, because the inverse has to put the
/// same clips back under the same id -- a relink of a different set would
/// serialise identically for the members it happened to get right.
class UnlinkClipsCommand final : public Command {
public:
    explicit UnlinkClipsCommand(LinkId link) : link_(link) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Unlink Clips"; }

    [[nodiscard]] Result<void> apply(Document& document) override {
        members_.clear();
        for (const Track& track : document.tracks()) {
            for (const Clip& clip : track.clips) {
                if (clip.link == link_) {
                    members_.push_back(clip.id);
                }
            }
        }
        return document.unlink(link_);
    }

    [[nodiscard]] Result<void> revert(Document& document) override {
        return document.relink(link_, members_);
    }

private:
    std::vector<ClipId> members_;
    LinkId link_;
};

/// Removal captures the whole object *and* where it sat, because restoring a
/// track at the wrong index changes the render order, and restoring a clip
/// without its flags loses user state. Both would pass a shallow check.
class RemoveTrackCommand final : public Command {
public:
    explicit RemoveTrackCommand(TrackId id) : id_(id) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Remove Track"; }

    [[nodiscard]] Result<void> apply(Document& document) override {
        const std::vector<Track>& tracks = document.tracks();
        const auto position = std::find_if(tracks.begin(), tracks.end(),
                                           [this](const Track& t) { return t.id == id_; });
        if (position == tracks.end()) {
            return Error{Errc::not_found, to_string(id_) + " does not exist"};
        }
        index_ = static_cast<std::size_t>(position - tracks.begin());

        Result<Track> removed = document.remove_track(id_);
        if (!removed) {
            return removed.error();
        }
        removed_ = std::move(removed).value();
        return ok();
    }

    [[nodiscard]] Result<void> revert(Document& document) override {
        return document.insert_track_at(index_, removed_);
    }

private:
    Track removed_;
    TrackId id_;
    std::size_t index_ = 0;
};

class RemoveClipCommand final : public Command {
public:
    explicit RemoveClipCommand(ClipId id) : id_(id) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Remove Clip"; }

    [[nodiscard]] Result<void> apply(Document& document) override {
        const Track* owner = document.track_of_clip(id_);
        if (owner == nullptr) {
            return Error{Errc::not_found, to_string(id_) + " does not exist"};
        }
        track_ = owner->id;

        Result<Clip> removed = document.remove_clip(id_);
        if (!removed) {
            return removed.error();
        }
        removed_ = std::move(removed).value();
        return ok();
    }

    [[nodiscard]] Result<void> revert(Document& document) override {
        return document.insert_clip(track_, removed_);
    }

private:
    Clip removed_;
    ClipId id_;
    TrackId track_;
};

class MoveClipCommand final : public Command {
public:
    MoveClipCommand(ClipId id, TrackId to_track, Ticks new_start)
        : id_(id), to_track_(to_track), new_start_(new_start) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Move Clip"; }

    [[nodiscard]] Result<void> apply(Document& document) override {
        const Track* owner = document.track_of_clip(id_);
        const Clip* clip = document.find_clip(id_);
        if (owner == nullptr || clip == nullptr) {
            return Error{Errc::not_found, to_string(id_) + " does not exist"};
        }
        from_track_ = owner->id;
        old_start_ = clip->start;
        return document.move_clip(id_, to_track_, new_start_);
    }

    [[nodiscard]] Result<void> revert(Document& document) override {
        return document.move_clip(id_, from_track_, old_start_);
    }

private:
    ClipId id_;
    TrackId to_track_;
    TrackId from_track_;
    Ticks new_start_;
    Ticks old_start_ = 0;
};

class SetClipBoundsCommand final : public Command {
public:
    SetClipBoundsCommand(ClipId id, Ticks source_in, Ticks start, Ticks duration)
        : id_(id), source_in_(source_in), start_(start), duration_(duration) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Trim Clip"; }

    [[nodiscard]] Result<void> apply(Document& document) override {
        const Clip* clip = document.find_clip(id_);
        if (clip == nullptr) {
            return Error{Errc::not_found, to_string(id_) + " does not exist"};
        }
        old_source_in_ = clip->source_in;
        old_start_ = clip->start;
        old_duration_ = clip->duration;
        return document.set_clip_bounds(id_, source_in_, start_, duration_);
    }

    [[nodiscard]] Result<void> revert(Document& document) override {
        return document.set_clip_bounds(id_, old_source_in_, old_start_, old_duration_);
    }

private:
    ClipId id_;
    Ticks source_in_;
    Ticks start_;
    Ticks duration_;
    Ticks old_source_in_ = 0;
    Ticks old_start_ = 0;
    Ticks old_duration_ = 0;
};

// Flag flips record the previous value rather than assuming it was the
// opposite: setting a flag to the value it already holds is a no-op whose
// inverse must also be a no-op, not a flip. Written out three times rather than
// factored into a template over a pointer-to-member: the template version needs
// explicit specialisations, which is exactly the corner where compilers
// disagree, and it saves fewer lines than it costs in portability risk.

class SetClipEnabledCommand final : public Command {
public:
    SetClipEnabledCommand(ClipId id, bool enabled) : id_(id), enabled_(enabled) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Enable Clip"; }

    [[nodiscard]] Result<void> apply(Document& document) override {
        const Clip* clip = document.find_clip(id_);
        if (clip == nullptr) {
            return Error{Errc::not_found, to_string(id_) + " does not exist"};
        }
        was_enabled_ = clip->enabled;
        return document.set_clip_enabled(id_, enabled_);
    }

    [[nodiscard]] Result<void> revert(Document& document) override {
        return document.set_clip_enabled(id_, was_enabled_);
    }

private:
    ClipId id_;
    bool enabled_;
    bool was_enabled_ = false;
};

class SetTrackMutedCommand final : public Command {
public:
    SetTrackMutedCommand(TrackId id, bool muted) : id_(id), muted_(muted) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Mute Track"; }

    [[nodiscard]] Result<void> apply(Document& document) override {
        const Track* track = document.find_track(id_);
        if (track == nullptr) {
            return Error{Errc::not_found, to_string(id_) + " does not exist"};
        }
        was_muted_ = track->muted;
        return document.set_track_muted(id_, muted_);
    }

    [[nodiscard]] Result<void> revert(Document& document) override {
        return document.set_track_muted(id_, was_muted_);
    }

private:
    TrackId id_;
    bool muted_;
    bool was_muted_ = false;
};

class SetTrackSyncLockedCommand final : public Command {
public:
    SetTrackSyncLockedCommand(TrackId id, bool sync_locked) : id_(id), sync_locked_(sync_locked) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Sync Lock Track"; }

    [[nodiscard]] Result<void> apply(Document& document) override {
        const Track* track = document.find_track(id_);
        if (track == nullptr) {
            return Error{Errc::not_found, to_string(id_) + " does not exist"};
        }
        was_sync_locked_ = track->sync_locked;
        return document.set_track_sync_locked(id_, sync_locked_);
    }

    [[nodiscard]] Result<void> revert(Document& document) override {
        return document.set_track_sync_locked(id_, was_sync_locked_);
    }

private:
    TrackId id_;
    bool sync_locked_;
    bool was_sync_locked_ = false;
};

class SetTrackLockedCommand final : public Command {
public:
    SetTrackLockedCommand(TrackId id, bool locked) : id_(id), locked_(locked) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Lock Track"; }

    [[nodiscard]] Result<void> apply(Document& document) override {
        const Track* track = document.find_track(id_);
        if (track == nullptr) {
            return Error{Errc::not_found, to_string(id_) + " does not exist"};
        }
        was_locked_ = track->locked;
        return document.set_track_locked(id_, locked_);
    }

    [[nodiscard]] Result<void> revert(Document& document) override {
        return document.set_track_locked(id_, was_locked_);
    }

private:
    TrackId id_;
    bool locked_;
    bool was_locked_ = false;
};

}  // namespace

// --- stack -------------------------------------------------------------------

Result<void> CommandStack::execute(Document& document, std::unique_ptr<Command> command) {
    if (command == nullptr) {
        return Error{Errc::invalid_argument, "cannot execute a null command"};
    }
    if (Result<void> applied = command->apply(document); !applied) {
        // The command is dropped and the history untouched: a refused edit must
        // not become an undo entry that would reverse something that never
        // happened.
        return applied.error();
    }
    done_.push_back(std::move(command));
    undone_.clear();
    return ok();
}

Result<void> CommandStack::undo(Document& document) {
    if (done_.empty()) {
        return Error{Errc::not_found, "nothing to undo"};
    }
    std::unique_ptr<Command>& command = done_.back();
    if (Result<void> reverted = command->revert(document); !reverted) {
        // Leaving it on the undo stack would invite a retry against a document
        // the failed revert may have already touched.
        return reverted.error().with_context("undo");
    }
    undone_.push_back(std::move(done_.back()));
    done_.pop_back();
    return ok();
}

Result<void> CommandStack::redo(Document& document) {
    if (undone_.empty()) {
        return Error{Errc::not_found, "nothing to redo"};
    }
    std::unique_ptr<Command>& command = undone_.back();
    if (Result<void> applied = command->apply(document); !applied) {
        return applied.error().with_context("redo");
    }
    done_.push_back(std::move(undone_.back()));
    undone_.pop_back();
    return ok();
}

std::string_view CommandStack::undo_name() const noexcept {
    return done_.empty() ? std::string_view{} : done_.back()->name();
}

std::string_view CommandStack::redo_name() const noexcept {
    return undone_.empty() ? std::string_view{} : undone_.back()->name();
}

void CommandStack::clear() noexcept {
    done_.clear();
    undone_.clear();
}

// --- factories ---------------------------------------------------------------

std::unique_ptr<Command> make_add_track(TrackKind kind, std::string name) {
    return std::make_unique<AddTrackCommand>(kind, std::move(name));
}

std::unique_ptr<Command> make_remove_track(TrackId id) {
    return std::make_unique<RemoveTrackCommand>(id);
}

std::unique_ptr<Command> make_add_clip(TrackId track, std::string source, Ticks source_in,
                                       Ticks start, Ticks duration, Ticks source_duration) {
    return std::make_unique<AddClipCommand>(track, std::move(source), source_in, start, duration,
                                            source_duration);
}

std::unique_ptr<Command> make_remove_clip(ClipId id) {
    return std::make_unique<RemoveClipCommand>(id);
}

std::unique_ptr<Command> make_move_clip(ClipId id, TrackId to_track, Ticks new_start) {
    return std::make_unique<MoveClipCommand>(id, to_track, new_start);
}

std::unique_ptr<Command> make_set_clip_bounds(ClipId id, Ticks source_in, Ticks start,
                                              Ticks duration) {
    return std::make_unique<SetClipBoundsCommand>(id, source_in, start, duration);
}

std::unique_ptr<Command> make_set_clip_enabled(ClipId id, bool enabled) {
    return std::make_unique<SetClipEnabledCommand>(id, enabled);
}

std::unique_ptr<Command> make_set_track_muted(TrackId id, bool muted) {
    return std::make_unique<SetTrackMutedCommand>(id, muted);
}

std::unique_ptr<Command> make_set_track_locked(TrackId id, bool locked) {
    return std::make_unique<SetTrackLockedCommand>(id, locked);
}

std::unique_ptr<Command> make_link_clips(std::vector<ClipId> clips) {
    return std::make_unique<LinkClipsCommand>(std::move(clips));
}

std::unique_ptr<Command> make_unlink_clips(LinkId link) {
    return std::make_unique<UnlinkClipsCommand>(link);
}

std::unique_ptr<Command> make_set_track_sync_locked(TrackId id, bool sync_locked) {
    return std::make_unique<SetTrackSyncLockedCommand>(id, sync_locked);
}

}  // namespace rf::timeline
