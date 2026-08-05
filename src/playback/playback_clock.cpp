#include "rf/playback/playback_clock.hpp"

#include <limits>

namespace rf::playback {
namespace {

using media::Rational;
using media::Rounding;
using media::rescale;

/// Nanoseconds as a time base: one tick is 1/1'000'000'000 of a second.
const Rational kNanosecondBase{1, 1'000'000'000};

[[nodiscard]] Result<std::int64_t> checked_add(std::int64_t a, std::int64_t b) {
    if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) ||
        (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b)) {
        return Error{Errc::invalid_argument, "timeline position overflow"};
    }
    return a + b;
}

}  // namespace

Result<PlaybackClock> PlaybackClock::create(const Rational& frame_rate, std::int64_t start_frame) {
    if (frame_rate.is_zero()) {
        return Error{Errc::invalid_argument, "frame rate cannot be zero"};
    }
    if (frame_rate.numerator() < 0) {
        return Error{Errc::invalid_argument,
                     "frame rate cannot be negative; use a negative play rate to run backwards"};
    }

    PlaybackClock clock;
    clock.frame_rate_ = frame_rate;
    clock.anchor_frame_ = start_frame;
    return clock;
}

namespace {

/// Seconds occupied by one frame at the given rate of play. At rate 2 a frame
/// is on screen half as long; at rate -1 the duration is negative, which is what
/// carries the direction through the arithmetic below.
[[nodiscard]] Result<Rational> seconds_per_frame(const Rational& frame_rate,
                                                 const Rational& rate) {
    Result<Rational> frame_duration = frame_rate.inverse();
    if (!frame_duration) {
        return frame_duration.error().with_context("frame duration");
    }
    return frame_duration.value().divided_by(rate);
}

}  // namespace

Result<void> PlaybackClock::play(Nanoseconds now, const Rational& rate) {
    if (rate.is_zero()) {
        return Error{Errc::invalid_argument,
                     "a zero play rate is a pause; call pause() so there is no such state"};
    }

    // Re-anchor at the current position first, or changing rate mid-playback
    // would retroactively reinterpret all the time already elapsed.
    if (playing_) {
        Result<std::int64_t> current = frame_at(now);
        if (!current) {
            return current.error().with_context("play");
        }
        anchor_frame_ = current.value();
    }

    anchor_time_ = now;
    rate_ = rate;
    playing_ = true;
    return ok();
}

Result<void> PlaybackClock::pause(Nanoseconds now) {
    if (!playing_) {
        return ok();
    }
    Result<std::int64_t> current = frame_at(now);
    if (!current) {
        return current.error().with_context("pause");
    }
    anchor_frame_ = current.value();
    anchor_time_ = now;
    playing_ = false;
    return ok();
}

void PlaybackClock::seek(Nanoseconds now, std::int64_t frame) {
    anchor_frame_ = frame;
    anchor_time_ = now;
}

Result<std::int64_t> PlaybackClock::frame_at(Nanoseconds now) const {
    if (!playing_) {
        return anchor_frame_;
    }

    Result<Rational> per_frame = seconds_per_frame(frame_rate_, rate_);
    if (!per_frame) {
        return per_frame.error().with_context("frame_at");
    }

    const std::int64_t elapsed = (now - anchor_time_).count();

    // Floor, because frame N is on screen for [N, N+1) of its own duration, so
    // the frame showing at an instant is the one whose interval contains it.
    // Position is computed from elapsed time, never accumulated: that is what
    // stops a slow renderer from dragging the playhead with it.
    Result<std::int64_t> offset =
        rescale(elapsed, kNanosecondBase, per_frame.value(), Rounding::down);
    if (!offset) {
        return offset.error().with_context("frame_at");
    }
    return checked_add(anchor_frame_, offset.value());
}

Result<Nanoseconds> PlaybackClock::time_of_frame(std::int64_t frame) const {
    if (!playing_) {
        if (frame == anchor_frame_) {
            return anchor_time_;
        }
        return Error{Errc::invalid_argument,
                     "a paused clock has no due time for any frame but the current one"};
    }

    Result<Rational> per_frame = seconds_per_frame(frame_rate_, rate_);
    if (!per_frame) {
        return per_frame.error().with_context("time_of_frame");
    }

    Result<std::int64_t> delta = checked_add(frame, -anchor_frame_);
    if (!delta) {
        return delta.error().with_context("time_of_frame");
    }

    // This must return the FIRST instant at which `frame` is the one showing,
    // so that frame_at(time_of_frame(n)) == n. Rounding to nearest breaks that:
    // it can land a nanosecond before the true boundary, and frame_at floors to
    // the previous frame.
    //
    // Which direction to round depends on the direction of play. Forward, the
    // position increases with time and the boundary is the earliest instant at
    // or after the exact value, so round up. In reverse the position decreases,
    // the frame's interval ends at the exact value, and the instant wanted is
    // the latest one at or before it -- so round down.
    const bool forward = per_frame.value().numerator() > 0;
    Result<std::int64_t> elapsed = rescale(delta.value(), per_frame.value(), kNanosecondBase,
                                           forward ? Rounding::up : Rounding::down);
    if (!elapsed) {
        return elapsed.error().with_context("time_of_frame");
    }

    Result<std::int64_t> absolute = checked_add(anchor_time_.count(), elapsed.value());
    if (!absolute) {
        return absolute.error().with_context("time_of_frame");
    }
    return Nanoseconds{absolute.value()};
}

}  // namespace rf::playback
