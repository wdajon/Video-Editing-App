#include "rf/timeline/trim.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace rf::timeline {
namespace {

/// A clip and its neighbours on the same track, resolved once.
///
/// Every trim needs some subset of these, and every one of them needs to know
/// whether a neighbour is *butt-joined* rather than merely present: a clip with
/// a gap beside it slides into the gap, and a clip touching its neighbour pushes
/// it. Resolving the two cases in one place is what stops the four operations
/// from each inventing their own notion of adjacency.
struct Neighbourhood {
    const Track* track = nullptr;
    std::size_t index = 0;

    [[nodiscard]] const Clip& self() const noexcept { return track->clips[index]; }
    [[nodiscard]] const Clip* previous() const noexcept {
        return index == 0 ? nullptr : &track->clips[index - 1];
    }
    [[nodiscard]] const Clip* next() const noexcept {
        return index + 1 >= track->clips.size() ? nullptr : &track->clips[index + 1];
    }
    /// Free space before the clip: the gap to the previous clip, or to tick 0.
    [[nodiscard]] Ticks space_before() const noexcept {
        const Clip* before = previous();
        return before == nullptr ? self().start : self().start - (before->start + before->duration);
    }
    /// Free space after the clip. With nothing following, the only limit is how
    /// far the clip's end can move and stay representable -- see TrimRange.
    [[nodiscard]] Ticks space_after() const noexcept {
        const Clip& clip = self();
        const Clip* after = next();
        if (after == nullptr) {
            return (std::numeric_limits<Ticks>::max() - clip.start) - clip.duration;
        }
        return after->start - (clip.start + clip.duration);
    }
};

[[nodiscard]] Result<Neighbourhood> locate(const Document& document, ClipId id) {
    const Track* track = document.track_of_clip(id);
    if (track == nullptr) {
        return Error{Errc::not_found, to_string(id) + " does not exist"};
    }
    const auto position = std::find_if(track->clips.begin(), track->clips.end(),
                                       [id](const Clip& clip) { return clip.id == id; });
    Neighbourhood found;
    found.track = track;
    found.index = static_cast<std::size_t>(position - track->clips.begin());
    return found;
}

/// Media left after a clip's out point, in ticks. Always >= 0: the document
/// invariant in ADR 009 makes a negative value unrepresentable.
[[nodiscard]] constexpr Ticks tail(const Clip& clip) noexcept {
    return clip.source_duration - clip.source_in - clip.duration;
}

/// The furthest right the clips after `where` can shift and stay representable.
/// They all move by the same delta, so the last one is the binding constraint.
[[nodiscard]] Ticks room_downstream(const Neighbourhood& where) noexcept {
    if (where.index + 1 >= where.track->clips.size()) {
        return std::numeric_limits<Ticks>::max();
    }
    const Clip& last = where.track->clips.back();
    return (std::numeric_limits<Ticks>::max() - last.start) - last.duration;
}

[[nodiscard]] constexpr bool is_ripple(TrimKind kind) noexcept {
    return kind == TrimKind::ripple_in || kind == TrimKind::ripple_out;
}

/// Where the sequence lengthens or shortens, for either ripple edge.
///
/// It is the clip's out point in both cases, which is worth deriving for the in
/// edge: rippling the in point takes material off the head while the clip keeps
/// its start, so the clip's *end* is what retreats and the timeline closes up
/// from there. See docs/adr/010-sync-lock.md.
[[nodiscard]] constexpr Ticks ripple_point(const Clip& clip) noexcept {
    return clip.start + clip.duration;
}

/// How far the material at or after `point` on `track` may shift.
///
/// `std::nullopt` means the track has nothing at or after the point, so it
/// places no constraint at all -- distinct from "it may not move", and worth
/// keeping distinct so an empty track cannot silently veto every ripple.
struct ShiftRoom {
    Ticks min_shift;
    Ticks max_shift;
};

[[nodiscard]] Result<std::optional<ShiftRoom>> room_on_track(const Track& track, Ticks point) {
    for (const Clip& clip : track.clips) {
        // A clip across the point cannot move -- its head is pinned by material
        // before the point -- and cannot stay, because its tail is in the region
        // that moves. Refusing names the obstruction; shifting round it would
        // desync the rest of the track silently, which is the whole defect this
        // is here to fix.
        if (clip.start < point && point < clip.start + clip.duration) {
            return Error{Errc::invalid_argument,
                         to_string(clip.id) + " on " + to_string(track.id) +
                             " straddles the ripple point at " + std::to_string(point) +
                             "; drop that track's sync lock to ripple past it"};
        }
    }

    const auto first = std::find_if(track.clips.begin(), track.clips.end(),
                                    [point](const Clip& clip) { return clip.start >= point; });
    if (first == track.clips.end()) {
        return std::optional<ShiftRoom>{};
    }

    const Ticks room_before =
        first == track.clips.begin() ? 0 : (first - 1)->start + (first - 1)->duration;
    const Clip& last = track.clips.back();
    return std::optional<ShiftRoom>{ShiftRoom{
        room_before - first->start,
        (std::numeric_limits<Ticks>::max() - last.start) - last.duration}};
}

[[nodiscard]] bool contains(const std::vector<TrackId>& tracks, TrackId id) {
    return std::find(tracks.begin(), tracks.end(), id) != tracks.end();
}

/// Narrows `range` by what every sync-locked track allows, skipping the tracks
/// that hold a member of the trim. A ripple the music bed on A2 has no room for
/// cannot happen; a member's own track is moved by the trim itself and must not
/// be moved a second time by the sweep (ADR 011 decision 4).
[[nodiscard]] Result<TrimRange> narrow_by_sync_locked_tracks(const Document& document,
                                                             const std::vector<TrackId>& members,
                                                             Ticks point, TrimKind kind,
                                                             TrimRange range) {
    for (const Track& track : document.tracks()) {
        if (contains(members, track.id) || !track.sync_locked) {
            continue;
        }
        Result<std::optional<ShiftRoom>> room = room_on_track(track, point);
        if (!room) {
            return room.error();
        }
        if (!room.value()) {
            continue;
        }
        const ShiftRoom& shift = *room.value();
        // A ripple of the out edge shifts by +delta, of the in edge by -delta.
        // Both bounds are derived from non-negative positions, so negating them
        // cannot overflow.
        const Ticks low = kind == TrimKind::ripple_in ? -shift.max_shift : shift.min_shift;
        const Ticks high = kind == TrimKind::ripple_in ? -shift.min_shift : shift.max_shift;
        range.min_delta = std::max(range.min_delta, low);
        range.max_delta = std::min(range.max_delta, high);
    }
    return range;
}

/// What one clip's own track allows, before any other track is considered.
[[nodiscard]] Result<TrimRange> own_track_range(const Neighbourhood& where, TrimKind kind) {
    const Clip& self = where.self();
    TrimRange range;

    switch (kind) {
        case TrimKind::ripple_in:
            // The in point walks back to source tick 0 and forward until one
            // tick of the clip is left. The clip's out point does not move, so
            // the source's tail is not involved. Everything downstream moves
            // left, so it cannot run out of room.
            range.min_delta = -self.source_in;
            range.max_delta = self.duration - 1;
            return range;

        case TrimKind::ripple_out:
            range.min_delta = 1 - self.duration;
            range.max_delta = std::min(tail(self), room_downstream(where));
            return range;

        case TrimKind::roll: {
            const Clip* incoming = where.next();
            if (incoming == nullptr ||
                incoming->start != self.start + self.duration) {
                return Error{Errc::invalid_argument,
                             "roll needs a clip butt-joined after " + to_string(self.id)};
            }
            range.min_delta = std::max(1 - self.duration, -incoming->source_in);
            range.max_delta = std::min(tail(self), incoming->duration - 1);
            return range;
        }

        case TrimKind::slip:
            range.min_delta = -self.source_in;
            range.max_delta = tail(self);
            return range;

        case TrimKind::nudge:
            // Straight into the free space either side. Nothing else moves, so
            // the gaps are the whole of the limit.
            range.min_delta = -where.space_before();
            range.max_delta = where.space_after();
            return range;

        case TrimKind::slide: {
            // Each side contributes independently, so the range is the tightest
            // of what both allow. Start from the widest possible and narrow.
            range.min_delta = std::numeric_limits<Ticks>::min();
            range.max_delta = std::numeric_limits<Ticks>::max();

            const Clip* before = where.previous();
            if (before != nullptr && before->start + before->duration == self.start) {
                // Butt-joined: the previous clip's out point absorbs the slide,
                // so it bounds both directions -- shrinking it has a floor, and
                // extending it needs media that may not be there.
                range.min_delta = std::max(range.min_delta, 1 - before->duration);
                range.max_delta = std::min(range.max_delta, tail(*before));
            } else {
                range.min_delta = std::max(range.min_delta, -where.space_before());
            }

            const Clip* after = where.next();
            if (after != nullptr && after->start == self.start + self.duration) {
                range.min_delta = std::max(range.min_delta, -after->source_in);
                range.max_delta = std::min(range.max_delta, after->duration - 1);
            } else {
                range.max_delta = std::min(range.max_delta, where.space_after());
            }
            return range;
        }
    }

    return Error{Errc::internal, "unknown trim kind"};
}

/// The members of `clip`'s link group, each located on its track, with `clip`
/// itself first.
///
/// An unlinked clip is a group of one, so nothing downstream has to distinguish
/// the two cases. The named clip leads because bystander tracks ripple from its
/// out point: members are aligned when linked, but an earlier edit can leave a
/// group offset (ADR 011), and the clip the user acted on is the predictable
/// choice of reference.
[[nodiscard]] Result<std::vector<Neighbourhood>> locate_group(const Document& document,
                                                              ClipId clip) {
    const std::vector<ClipId> group = document.linked_clips(clip);
    if (group.empty()) {
        return Error{Errc::not_found, to_string(clip) + " does not exist"};
    }
    std::vector<Neighbourhood> members;
    members.reserve(group.size());
    for (const ClipId id : group) {
        Result<Neighbourhood> where = locate(document, id);
        if (!where) {
            return where.error();
        }
        if (id == clip) {
            members.insert(members.begin(), where.value());
        } else {
            members.push_back(where.value());
        }
    }
    return members;
}

/// What the whole group allows: the intersection of every member's own track,
/// then narrowed by the sync-locked tracks that hold no member.
///
/// The intersection is the point of the whole feature. If the audio has twenty
/// ticks of tail and the picture has two hundred, the ripple stops at twenty and
/// the pair stays together.
[[nodiscard]] Result<TrimRange> group_range(const Document& document,
                                            const std::vector<Neighbourhood>& members,
                                            TrimKind kind) {
    TrimRange range{std::numeric_limits<Ticks>::min(), std::numeric_limits<Ticks>::max()};
    std::vector<TrackId> tracks;
    tracks.reserve(members.size());

    for (const Neighbourhood& member : members) {
        Result<TrimRange> own = own_track_range(member, kind);
        if (!own) {
            // A roll with no butt-joined neighbour, say. Name the member, since
            // it may be on a track the user was not looking at.
            return own.error().with_context(to_string(member.self().id));
        }
        range.min_delta = std::max(range.min_delta, own.value().min_delta);
        range.max_delta = std::min(range.max_delta, own.value().max_delta);
        tracks.push_back(member.track->id);
    }

    if (!is_ripple(kind)) {
        return range;
    }
    return narrow_by_sync_locked_tracks(document, tracks, ripple_point(members.front().self()),
                                        kind, range);
}

/// Applies `kind` to a copy of the trimmed track's clips. `delta` is clamped.
[[nodiscard]] std::vector<Clip> rewrite_own_track(const Neighbourhood& where, TrimKind kind,
                                                  Ticks delta) {
    std::vector<Clip> clips = where.track->clips;
    Clip& self = clips[where.index];

    switch (kind) {
        case TrimKind::ripple_in:
            self.source_in += delta;
            self.duration -= delta;
            // The clip keeps its start; its out point moved by -delta, so
            // everything after it follows by the same amount.
            for (std::size_t i = where.index + 1; i < clips.size(); ++i) {
                clips[i].start -= delta;
            }
            break;

        case TrimKind::ripple_out:
            self.duration += delta;
            for (std::size_t i = where.index + 1; i < clips.size(); ++i) {
                clips[i].start += delta;
            }
            break;

        case TrimKind::roll: {
            Clip& incoming = clips[where.index + 1];
            self.duration += delta;
            incoming.start += delta;
            incoming.source_in += delta;
            incoming.duration -= delta;
            break;
        }

        case TrimKind::slip:
            self.source_in += delta;
            break;

        case TrimKind::nudge:
            self.start += delta;
            break;

        case TrimKind::slide: {
            const Ticks self_start = self.start;
            const Ticks self_end = self.start + self.duration;
            self.start += delta;
            if (where.index > 0) {
                Clip& before = clips[where.index - 1];
                if (before.start + before.duration == self_start) {
                    before.duration += delta;
                }
            }
            if (where.index + 1 < clips.size()) {
                Clip& after = clips[where.index + 1];
                if (after.start == self_end) {
                    after.start += delta;
                    after.source_in += delta;
                    after.duration -= delta;
                }
            }
            break;
        }
    }

    return clips;
}

/// Every track the operation rewrites: one per link member, then the sync-locked
/// tracks that hold no member.
///
/// Only a ripple reaches beyond the members' own tracks. Roll, slip and slide
/// all leave the sequence the same length, so there is no downstream material to
/// move and nothing for a sync lock to do.
[[nodiscard]] std::vector<TrackClips> rewrite_all(const Document& document,
                                                  const std::vector<Neighbourhood>& members,
                                                  TrimKind kind, Ticks delta) {
    std::vector<TrackClips> rewrites;
    std::vector<TrackId> member_tracks;
    for (const Neighbourhood& member : members) {
        rewrites.push_back(
            TrackClips{member.track->id, rewrite_own_track(member, kind, delta)});
        member_tracks.push_back(member.track->id);
    }
    if (!is_ripple(kind)) {
        return rewrites;
    }

    const Ticks point = ripple_point(members.front().self());
    const Ticks shift = kind == TrimKind::ripple_in ? -delta : delta;
    for (const Track& track : document.tracks()) {
        // A member's track was already moved by the trim. Shifting it again here
        // would move it twice -- ADR 011 decision 4.
        if (contains(member_tracks, track.id) || !track.sync_locked) {
            continue;
        }
        std::vector<Clip> clips = track.clips;
        bool moved = false;
        for (Clip& clip : clips) {
            if (clip.start >= point) {
                clip.start += shift;
                moved = true;
            }
        }
        // A track with nothing after the point is not part of the edit, and
        // listing it would put an unchanged vector into the undo record.
        if (moved) {
            rewrites.push_back(TrackClips{track.id, std::move(clips)});
        }
    }
    return rewrites;
}

/// Every trim is the same shape: work out how far it can go, rewrite the tracks
/// it touches, and hand their previous clip vectors to undo. Atomicity comes
/// from `Document::replace_clips`, which validates every rewrite before
/// installing any of them.
class TrimCommand final : public Command {
public:
    TrimCommand(ClipId clip, TrimKind kind, Ticks delta)
        : clip_(clip), kind_(kind), requested_(delta) {}

    [[nodiscard]] std::string_view name() const noexcept override { return to_string(kind_); }

    [[nodiscard]] Result<void> apply(Document& document) override {
        Result<std::vector<Neighbourhood>> members = locate_group(document, clip_);
        if (!members) {
            return members.error();
        }
        Result<TrimRange> range = group_range(document, members.value(), kind_);
        if (!range) {
            return range.error();
        }

        const Ticks applied = clamp_delta(range.value(), requested_);
        if (applied == 0) {
            return Error{Errc::invalid_argument,
                         std::string{to_string(kind_)} + " of " + to_string(clip_) + " by " +
                             std::to_string(requested_) + " ticks has no room to move"};
        }

        std::vector<TrackClips> rewrites = rewrite_all(document, members.value(), kind_, applied);
        // Capture the previous state of exactly the tracks about to change --
        // undo has to put back every one of them, not just the trimmed one.
        before_.clear();
        before_.reserve(rewrites.size());
        for (const TrackClips& rewrite : rewrites) {
            before_.push_back(TrackClips{rewrite.track, document.find_track(rewrite.track)->clips});
        }
        return document.replace_clips(std::move(rewrites));
    }

    [[nodiscard]] Result<void> revert(Document& document) override {
        return document.replace_clips(before_);
    }

private:
    std::vector<TrackClips> before_;
    ClipId clip_;
    TrimKind kind_;
    Ticks requested_;
};

}  // namespace

std::string_view to_string(TrimKind kind) noexcept {
    switch (kind) {
        case TrimKind::ripple_in:  return "Ripple Trim In";
        case TrimKind::ripple_out: return "Ripple Trim Out";
        case TrimKind::roll:       return "Roll Edit";
        case TrimKind::slip:       return "Slip";
        case TrimKind::slide:      return "Slide";
        case TrimKind::nudge:      return "Move Clip";
    }
    return "Trim";
}

Ticks clamp_delta(const TrimRange& range, Ticks delta) noexcept {
    return std::clamp(delta, range.min_delta, range.max_delta);
}

Result<TrimRange> trim_range(const Document& document, ClipId clip, TrimKind kind) {
    Result<std::vector<Neighbourhood>> members = locate_group(document, clip);
    if (!members) {
        return members.error();
    }
    return group_range(document, members.value(), kind);
}

Result<std::vector<TrackClips>> plan_trim(const Document& document, ClipId clip, TrimKind kind,
                                          Ticks delta) {
    Result<std::vector<Neighbourhood>> members = locate_group(document, clip);
    if (!members) {
        return members.error();
    }
    Result<TrimRange> range = group_range(document, members.value(), kind);
    if (!range) {
        return range.error();
    }
    return rewrite_all(document, members.value(), kind, clamp_delta(range.value(), delta));
}

std::unique_ptr<Command> make_trim(ClipId clip, TrimKind kind, Ticks delta) {
    return std::make_unique<TrimCommand>(clip, kind, delta);
}

}  // namespace rf::timeline
