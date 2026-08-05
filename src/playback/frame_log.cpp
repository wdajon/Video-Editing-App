#include "rf/playback/frame_log.hpp"

#include <algorithm>
#include <cstddef>

namespace rf::playback {
namespace {

[[nodiscard]] Nanoseconds percentile(const std::vector<Nanoseconds>& sorted, double fraction) {
    if (sorted.empty()) {
        return Nanoseconds{0};
    }
    const double last = static_cast<double>(sorted.size() - 1);
    const auto index = static_cast<std::size_t>(std::min(last, fraction * last));
    return sorted[index];
}

}  // namespace

FrameLog::FrameLog(std::size_t expected_frames) {
    entries_.reserve(expected_frames);
}

void FrameLog::record(std::int64_t frame, Nanoseconds when, Nanoseconds due) {
    Entry entry;

    if (have_previous_) {
        entry.interval = when - previous_time_;

        // Frames the playhead moved through that never reached the screen.
        // Only forward gaps count: going backwards is a discontinuity, and a
        // caller that failed to mark one should not have it silently recorded
        // as a huge number of drops.
        const std::int64_t gap = frame - previous_frame_;
        if (gap > 1) {
            dropped_ += gap - 1;
        }
    }

    // A frame presented before it was due is on time. Lateness is clamped at
    // zero rather than allowed to go negative, so early frames cannot offset
    // late ones in a percentile.
    entry.lateness = when > due ? (when - due) : Nanoseconds{0};

    entries_.push_back(entry);
    ++presented_;
    previous_frame_ = frame;
    previous_time_ = when;
    have_previous_ = true;
}

void FrameLog::record(std::int64_t frame, Nanoseconds when) {
    // No due time: treat it as exactly on time, and let the interval still be
    // measured against the previous presentation.
    record(frame, when, when);
}

void FrameLog::mark_discontinuity() noexcept {
    have_previous_ = false;
}

void FrameLog::clear() noexcept {
    entries_.clear();
    presented_ = 0;
    dropped_ = 0;
    have_previous_ = false;
}

FrameStatistics FrameLog::statistics(Nanoseconds late_threshold) const {
    FrameStatistics stats;
    stats.presented = presented_;
    stats.dropped = dropped_;

    std::vector<Nanoseconds> intervals;
    std::vector<Nanoseconds> lateness;
    intervals.reserve(entries_.size());
    lateness.reserve(entries_.size());

    for (const Entry& entry : entries_) {
        if (entry.interval > Nanoseconds{0}) {
            intervals.push_back(entry.interval);
        }
        lateness.push_back(entry.lateness);
        if (entry.lateness > late_threshold) {
            ++stats.late;
        }
    }

    std::sort(intervals.begin(), intervals.end());
    std::sort(lateness.begin(), lateness.end());

    stats.interval_p50 = percentile(intervals, 0.50);
    stats.interval_p99 = percentile(intervals, 0.99);
    stats.interval_max = intervals.empty() ? Nanoseconds{0} : intervals.back();
    stats.lateness_p99 = percentile(lateness, 0.99);
    stats.lateness_max = lateness.empty() ? Nanoseconds{0} : lateness.back();
    return stats;
}

}  // namespace rf::playback
