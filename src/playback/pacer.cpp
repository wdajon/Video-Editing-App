#include "rf/playback/pacer.hpp"

#include <algorithm>

namespace rf::playback {

Result<Pacer::Tick> Pacer::wait_next() {
    if (!playback_->is_playing()) {
        return Error{Errc::invalid_argument,
                     "the playhead is paused, so there is no next frame to wait for"};
    }

    // The frame this loop would like to show next. On the first call, and after
    // a seek, take it from the clock instead.
    std::int64_t wanted = previous_frame_ + 1;
    if (!have_previous_) {
        Result<std::int64_t> current = playback_->frame_at(clock_->now());
        if (!current) {
            return current.error().with_context("pacer");
        }
        wanted = current.value();
    }

    Result<Nanoseconds> due = playback_->time_of_frame(wanted);
    if (!due) {
        return due.error().with_context("pacer");
    }

    clock_->sleep_until(due.value());

    // After waiting, ask the clock what should actually be on screen. If
    // rendering kept up this is exactly `wanted`; if it did not, the clock has
    // moved past frames that were never produced. Skipping to the current frame
    // rather than rendering the stale one is what keeps playback in sync --
    // falling one frame further behind on every slow frame would drift without
    // bound.
    Result<std::int64_t> current = playback_->frame_at(clock_->now());
    if (!current) {
        return current.error().with_context("pacer");
    }

    Tick tick;
    tick.frame = std::max(wanted, current.value());

    Result<Nanoseconds> actual_due = playback_->time_of_frame(tick.frame);
    if (!actual_due) {
        return actual_due.error().with_context("pacer");
    }
    tick.due = actual_due.value();
    return tick;
}

void Pacer::presented(const Tick& tick, Nanoseconds when) {
    // FrameLog counts the gap between consecutive presentations as drops, so
    // skipped frames are recorded here without the pacer counting them itself.
    log_->record(tick.frame, when, tick.due);
    previous_frame_ = tick.frame;
    have_previous_ = true;
}

}  // namespace rf::playback
