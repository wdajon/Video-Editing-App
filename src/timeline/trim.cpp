#include "rf/timeline/trim.hpp"

#include <algorithm>
#include <limits>
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

[[nodiscard]] Result<TrimRange> range_of(const Neighbourhood& where, TrimKind kind) {
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

/// Applies `kind` to a copy of the track's clips. `delta` is already clamped.
[[nodiscard]] std::vector<Clip> rewrite(const Neighbourhood& where, TrimKind kind, Ticks delta) {
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

/// Every trim is the same shape: work out how far it can go, rewrite the track,
/// and hand the previous clip vector to undo. Atomicity comes from
/// `Document::replace_track_clips`, which validates the whole vector before
/// installing any of it.
class TrimCommand final : public Command {
public:
    TrimCommand(ClipId clip, TrimKind kind, Ticks delta)
        : clip_(clip), kind_(kind), requested_(delta) {}

    [[nodiscard]] std::string_view name() const noexcept override { return to_string(kind_); }

    [[nodiscard]] Result<void> apply(Document& document) override {
        Result<Neighbourhood> where = locate(document, clip_);
        if (!where) {
            return where.error();
        }
        Result<TrimRange> range = range_of(where.value(), kind_);
        if (!range) {
            return range.error();
        }

        const Ticks applied = clamp_delta(range.value(), requested_);
        if (applied == 0) {
            return Error{Errc::invalid_argument,
                         std::string{to_string(kind_)} + " of " + to_string(clip_) + " by " +
                             std::to_string(requested_) + " ticks has no room to move"};
        }

        track_ = where.value().track->id;
        before_ = where.value().track->clips;
        return document.replace_track_clips(track_, rewrite(where.value(), kind_, applied));
    }

    [[nodiscard]] Result<void> revert(Document& document) override {
        return document.replace_track_clips(track_, before_);
    }

private:
    std::vector<Clip> before_;
    ClipId clip_;
    TrackId track_;
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
    }
    return "Trim";
}

Ticks clamp_delta(const TrimRange& range, Ticks delta) noexcept {
    return std::clamp(delta, range.min_delta, range.max_delta);
}

Result<TrimRange> trim_range(const Document& document, ClipId clip, TrimKind kind) {
    Result<Neighbourhood> where = locate(document, clip);
    if (!where) {
        return where.error();
    }
    return range_of(where.value(), kind);
}

Result<std::vector<Clip>> plan_trim(const Document& document, ClipId clip, TrimKind kind,
                                    Ticks delta) {
    Result<Neighbourhood> where = locate(document, clip);
    if (!where) {
        return where.error();
    }
    Result<TrimRange> range = range_of(where.value(), kind);
    if (!range) {
        return range.error();
    }
    return rewrite(where.value(), kind, clamp_delta(range.value(), delta));
}

std::unique_ptr<Command> make_trim(ClipId clip, TrimKind kind, Ticks delta) {
    return std::make_unique<TrimCommand>(clip, kind, delta);
}

}  // namespace rf::timeline
