// The four trims: ripple, roll, slip, slide.
//
// Each is defined exactly in docs/adr/009-trim-model.md, together with why the
// definitions are written down at all -- "ripple trim the in point" has two
// readings that disagree about where the new head material appears.
//
// The reachable range of each operation is public, not an implementation detail
// of the commands, because the UI needs the same numbers: a drag has to stop at
// the same tick a keyboard trim refuses to pass, or the two disagree about where
// the media ends.

#ifndef RF_TIMELINE_TRIM_HPP
#define RF_TIMELINE_TRIM_HPP

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "rf/core/result.hpp"
#include "rf/timeline/command.hpp"
#include "rf/timeline/document.hpp"

namespace rf::timeline {

/// The trim set. One enumeration rather than four function families because a
/// command map binds keys to exactly this: an operation, a clip, and a delta.
enum class TrimKind : std::uint8_t {
    /// Move the clip's in point; later clips on the track follow.
    ripple_in = 0,
    /// Move the clip's out point; later clips on the track follow.
    ripple_out = 1,
    /// Move the edit point between this clip and the next. The clip named is the
    /// **outgoing** side; the two must be butt-joined.
    roll = 2,
    /// Move the source material inside the clip. The timeline does not change.
    slip = 3,
    /// Move the clip along the timeline; its neighbours absorb the difference.
    slide = 4,
    /// Move the clip into free space beside it, changing nothing else. Premiere
    /// calls this nudging. Unlike a slide, no neighbour gives anything up, so it
    /// is bounded by the gaps rather than by the neighbours' media.
    nudge = 5,
};

[[nodiscard]] std::string_view to_string(TrimKind kind) noexcept;

/// How far an operation can move, as a signed inclusive range of deltas.
///
/// Every range is finite. A clip with nothing after it looks like it could slide
/// right forever, but a tick is an `std::int64_t` and a clip's end has to stay
/// representable -- an invariant `Document` enforces. The bound in that case is
/// therefore real rather than invented, and stating it as a number is what keeps
/// the arithmetic below plain signed addition with nothing to saturate.
///
/// A range always contains 0 -- doing nothing is always reachable -- so an
/// operation that is applicable but has no room is `{0, 0}` rather than an error.
struct TrimRange {
    Ticks min_delta = 0;
    Ticks max_delta = 0;

    [[nodiscard]] bool allows(Ticks delta) const noexcept {
        return delta >= min_delta && delta <= max_delta;
    }
    [[nodiscard]] bool is_empty() const noexcept { return min_delta == 0 && max_delta == 0; }

    friend bool operator==(const TrimRange&, const TrimRange&) = default;
};

/// The largest movement in the requested direction that `range` permits.
[[nodiscard]] Ticks clamp_delta(const TrimRange& range, Ticks delta) noexcept;

/// The reachable range of `kind` on `clip`.
///
/// Fails only when the operation is not applicable at all -- an unknown clip, a
/// roll with no butt-joined clip after it, or a ripple obstructed by a clip
/// straddling the ripple point on a sync-locked track (ADR 010). An applicable
/// operation with no room returns an empty range, because "you are at the media
/// limit" is a different answer from "that is not an edit".
///
/// A ripple's range is the intersection of what every affected track allows: the
/// music bed on A2 has to have somewhere to go, or the edit cannot happen.
[[nodiscard]] Result<TrimRange> trim_range(const Document& document, ClipId clip, TrimKind kind);

/// The clips `kind` would leave behind, with `delta` clamped to the reachable
/// range. Exposed so the UI can draw a trim before committing it, and so tests
/// can assert the arithmetic without going through the command stack.
///
/// Each entry is a *whole* track, because a ripple moves every later clip on it.
/// A ripple returns one entry per affected track -- the trimmed one plus every
/// sync-locked track (ADR 010); roll, slip and slide return exactly one.
[[nodiscard]] Result<std::vector<TrackClips>> plan_trim(const Document& document, ClipId clip,
                                                        TrimKind kind, Ticks delta);

/// A trim as an undoable command.
///
/// Applies to every member of `clip`'s link group, which is what keeps a picture
/// and its audio together (ADR 011). An unlinked clip is a group of one.
///
/// `delta` is clamped to the reachable range, as Premiere does -- a trim runs as
/// far as it can rather than refusing wholesale. A request that clamps to zero
/// fails, so it never becomes an undo entry for an edit that did not happen.
[[nodiscard]] std::unique_ptr<Command> make_trim(ClipId clip, TrimKind kind, Ticks delta);

}  // namespace rf::timeline

#endif  // RF_TIMELINE_TRIM_HPP
