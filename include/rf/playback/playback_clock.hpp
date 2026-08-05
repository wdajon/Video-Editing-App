// Maps wall time onto a timeline position.
//
// Position is a pure function of elapsed time since an anchor -- it is never
// accumulated, so it cannot drift, and a stalled renderer changes what the user
// sees without changing where playback actually is. That property is what makes
// a dropped frame detectable instead of quietly absorbed.
//
// See docs/adr/006-playback-clock-and-frame-accounting.md.

#ifndef RF_PLAYBACK_PLAYBACK_CLOCK_HPP
#define RF_PLAYBACK_PLAYBACK_CLOCK_HPP

#include <cstdint>

#include "rf/core/result.hpp"
#include "rf/media/rational.hpp"
#include "rf/playback/clock.hpp"

namespace rf::playback {

/// Tracks where the playhead is, given a frame rate and a rate of play.
class PlaybackClock {
public:
    /// `frame_rate` is the sequence rate, e.g. 30000/1001. Fails if it is zero
    /// or negative -- neither describes a playable sequence.
    [[nodiscard]] static Result<PlaybackClock> create(const media::Rational& frame_rate,
                                                      std::int64_t start_frame = 0);

    [[nodiscard]] const media::Rational& frame_rate() const noexcept { return frame_rate_; }
    [[nodiscard]] bool is_playing() const noexcept { return playing_; }

    /// Rate of play: 1 is normal, 2 is double speed, -1 is reverse. Meaningful
    /// only while playing.
    [[nodiscard]] const media::Rational& rate() const noexcept { return rate_; }

    /// Starts (or restarts) playback at `rate`, anchored at `now`.
    /// A zero rate is rejected: that is a pause, and pausing has its own call
    /// so that "playing at zero speed" cannot become a state to reason about.
    [[nodiscard]] Result<void> play(Nanoseconds now, const media::Rational& rate);

    /// Stops the playhead where it currently is.
    ///
    /// Returns a Result because it has to evaluate the current position to
    /// anchor there, and that evaluation can fail on an absurd timeline length.
    /// Swallowing that would leave the playhead silently at a stale position.
    [[nodiscard]] Result<void> pause(Nanoseconds now);

    /// Jumps to `frame` and re-anchors. Frames skipped by a seek were never due
    /// to be shown, so they are not drops.
    void seek(Nanoseconds now, std::int64_t frame);

    /// The frame that should be on screen at `now`.
    ///
    /// Frame N occupies [N, N+1) of its own duration, so this floors: the frame
    /// showing at an instant is the one whose interval contains it.
    [[nodiscard]] Result<std::int64_t> frame_at(Nanoseconds now) const;

    /// Wall instant at which `frame` becomes due, under the current anchor and
    /// rate. Used by the scheduler to decide how long it may sleep.
    [[nodiscard]] Result<Nanoseconds> time_of_frame(std::int64_t frame) const;

private:
    PlaybackClock() = default;

    media::Rational frame_rate_{30, 1};
    media::Rational rate_{1, 1};
    Nanoseconds anchor_time_{0};
    std::int64_t anchor_frame_ = 0;
    bool playing_ = false;
};

}  // namespace rf::playback

#endif  // RF_PLAYBACK_PLAYBACK_CLOCK_HPP
