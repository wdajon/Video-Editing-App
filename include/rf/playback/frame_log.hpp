// Frame presentation accounting: what reached the screen, when, and what did not.
//
// M3's gate is "sustained 30 fps, no dropped frames over 60 s". An average frame
// rate cannot support that claim -- 1,800 frames in 60 seconds averages 30 fps
// and can stutter continuously. This records each presentation individually so
// the claim is about the worst moment, not the mean.
//
// See docs/adr/006-playback-clock-and-frame-accounting.md for what counts as a
// drop and, just as importantly, what does not.

#ifndef RF_PLAYBACK_FRAME_LOG_HPP
#define RF_PLAYBACK_FRAME_LOG_HPP

#include <cstdint>
#include <vector>

#include "rf/playback/clock.hpp"

namespace rf::playback {

struct FrameStatistics {
    std::int64_t presented = 0;
    std::int64_t dropped = 0;
    std::int64_t late = 0;

    /// Present-to-present intervals, in nanoseconds.
    Nanoseconds interval_p50{0};
    Nanoseconds interval_p99{0};
    Nanoseconds interval_max{0};

    /// How far behind its due time each frame arrived. Never negative: a frame
    /// presented early is on time, not "negatively late".
    Nanoseconds lateness_p99{0};
    Nanoseconds lateness_max{0};
};

/// Records presentations and derives the numbers the gate is stated in.
class FrameLog {
public:
    /// Reserves room for `expected_frames` so recording does not allocate
    /// mid-playback. Presentation is called from the render loop, and an
    /// allocation there is a frame-time spike of its own making.
    explicit FrameLog(std::size_t expected_frames = 0);

    /// Records that `frame` was put on screen at `when`, having been due at
    /// `due`. `due` may be past `when` -- that is an early frame, not a late one.
    void record(std::int64_t frame, Nanoseconds when, Nanoseconds due);

    /// Records a presentation with no known due time, e.g. the first frame
    /// after a seek. Counted as presented, never as late.
    void record(std::int64_t frame, Nanoseconds when);

    /// Marks a discontinuity: a seek, a rate change, or a pause. Frames the
    /// playhead jumped over were never due, so they must not be counted as
    /// drops.
    void mark_discontinuity() noexcept;

    [[nodiscard]] FrameStatistics statistics(Nanoseconds late_threshold) const;

    [[nodiscard]] std::int64_t presented_count() const noexcept { return presented_; }
    [[nodiscard]] std::int64_t dropped_count() const noexcept { return dropped_; }

    void clear() noexcept;

private:
    struct Entry {
        Nanoseconds interval{0};
        Nanoseconds lateness{0};
    };

    std::vector<Entry> entries_;
    std::int64_t presented_ = 0;
    std::int64_t dropped_ = 0;
    std::int64_t previous_frame_ = 0;
    Nanoseconds previous_time_{0};
    bool have_previous_ = false;
};

}  // namespace rf::playback

#endif  // RF_PLAYBACK_FRAME_LOG_HPP
