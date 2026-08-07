// What is on screen at a given frame.
//
// The question nothing could answer until now. Playback, the Program monitor and
// the export path all need the same thing -- given a playhead, which clips are
// visible, in what order, and which frame of each source does each one show --
// and none of them should work it out for itself. See docs/adr/018-composition.md.
//
// This is document logic with no decoder, no GPU and no window in it, so the
// arithmetic that decides which source frame appears is testable on its own.

#ifndef RF_TIMELINE_COMPOSITION_HPP
#define RF_TIMELINE_COMPOSITION_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "rf/core/result.hpp"
#include "rf/timeline/document.hpp"

namespace rf::timeline {

/// One visible clip at an instant, and the source frame it shows.
struct Layer {
    ClipId clip;
    TrackId track;
    std::string source;
    /// Frame index into `source`, floored. Frame N occupies [N, N+1) of its own
    /// duration, so the frame showing at an instant is the one whose interval
    /// contains it -- the same convention as `PlaybackClock` (ADR 006).
    std::int64_t source_frame = 0;

    friend bool operator==(const Layer&, const Layer&) = default;
};

/// The video layers visible at `frame`, bottom track first.
///
/// Bottom first because that is compositing order: V1 is the base and each
/// higher track draws over it. `Document::tracks()` holds them in that order
/// already, so the two cannot disagree.
///
/// Empty is a legitimate answer, not an error: a playhead in a gap, or past the
/// end of the sequence, shows nothing. A renderer must draw black there rather
/// than hold the last frame, which is how a stale picture gets mistaken for a
/// live one.
///
/// Audio tracks are absent. They carry no picture, and including them would put
/// the question of what a layer means on the caller.
///
/// Disabled clips are absent too -- that is what disabling one means.
[[nodiscard]] Result<std::vector<Layer>> layers_at(const Document& document,
                                                   std::int64_t frame);

/// Last frame with anything on it, or 0 for an empty sequence.
///
/// The end of the sequence, in frames. Used to stop playback running forever
/// past the end of the edit.
[[nodiscard]] std::int64_t sequence_end_frame(const Document& document);

}  // namespace rf::timeline

#endif  // RF_TIMELINE_COMPOSITION_HPP
