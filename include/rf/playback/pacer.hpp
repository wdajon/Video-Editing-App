// The paced playback loop.
//
// Turns "which frame should be on screen now" into a loop a renderer can drive:
// wait until the next frame is due, render whatever the clock says is current,
// record what actually reached the screen.
//
// The distinction that matters is between the frame the loop *wanted* to show
// next and the frame the clock says is current. When rendering keeps up they
// are the same. When it does not, the clock has moved past frames that were
// never produced, and those are drops -- the loop skips to the current frame
// rather than falling further behind, which is what an editor must do to stay
// in sync with audio.
//
// See docs/adr/006-playback-clock-and-frame-accounting.md.

#ifndef RF_PLAYBACK_PACER_HPP
#define RF_PLAYBACK_PACER_HPP

#include <cstdint>

#include "rf/core/result.hpp"
#include "rf/playback/clock.hpp"
#include "rf/playback/frame_log.hpp"
#include "rf/playback/playback_clock.hpp"

namespace rf::playback {

class Pacer {
public:
    /// Borrows all three; none may outlive the pacer.
    Pacer(Clock& clock, PlaybackClock& playback, FrameLog& log) noexcept
        : clock_(&clock), playback_(&playback), log_(&log) {}

    struct Tick {
        /// The frame to render.
        std::int64_t frame = 0;
        /// When that frame became due.
        Nanoseconds due{0};
    };

    /// Waits until the next frame is due and returns it.
    ///
    /// Fails if the clock is paused -- a paused playhead has no next frame, and
    /// returning the current one forever would turn a stopped editor into a
    /// busy loop.
    [[nodiscard]] Result<Tick> wait_next();

    /// Records that `tick` reached the screen. Call after rendering, with the
    /// time it was actually shown.
    void presented(const Tick& tick, Nanoseconds when);

    /// Drops the loop's memory of where it was, so the next wait re-reads the
    /// clock. Call after a seek or a rate change, alongside
    /// FrameLog::mark_discontinuity().
    void resynchronise() noexcept { have_previous_ = false; }

private:
    Clock* clock_;
    PlaybackClock* playback_;
    FrameLog* log_;
    std::int64_t previous_frame_ = 0;
    bool have_previous_ = false;
};

}  // namespace rf::playback

#endif  // RF_PLAYBACK_PACER_HPP
