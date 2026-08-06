// Time sources for playback.
//
// The clock is an interface so tests can drive it. A playback test that sleeps
// is slow, flaky, and cannot exercise a 60-second sustained run without taking
// 60 seconds; a test that advances a ManualClock simulates an hour in
// milliseconds and can place a late frame at an exact instant.
//
// See docs/adr/006-playback-clock-and-frame-accounting.md.

#ifndef RF_PLAYBACK_CLOCK_HPP
#define RF_PLAYBACK_CLOCK_HPP

#include <chrono>
#include <cstdint>
#include <thread>

namespace rf::playback {

using Nanoseconds = std::chrono::nanoseconds;

class Clock {
public:
    virtual ~Clock() = default;
    Clock(const Clock&) = delete;
    Clock& operator=(const Clock&) = delete;

    /// Monotonic time since an unspecified epoch. Only differences are
    /// meaningful.
    [[nodiscard]] virtual Nanoseconds now() const noexcept = 0;

    /// Blocks until `deadline`, or returns immediately if it has passed.
    ///
    /// Part of the Clock interface rather than a free function so that a test
    /// can make waiting instantaneous: ManualClock simply jumps to the
    /// deadline, which lets a sixty-second paced playback scenario run in
    /// milliseconds and deterministically. A real sleep in a test is slow and
    /// flaky, and one that cannot be controlled cannot simulate a slow
    /// renderer at all.
    virtual void sleep_until(Nanoseconds deadline) = 0;

protected:
    Clock() = default;
};

/// The real clock.
///
/// steady_clock, never system_clock: an NTP correction during playback would
/// otherwise look like thousands of dropped frames, or like time running
/// backwards.
class SteadyClock final : public Clock {
public:
    [[nodiscard]] Nanoseconds now() const noexcept override {
        return std::chrono::duration_cast<Nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch());
    }

    void sleep_until(Nanoseconds deadline) override {
        // Sleeping is what a headless paced loop has to do. Once frames are
        // presented through a swapchain, the wait belongs on the presentation
        // engine instead -- blocking on vsync is both more accurate and what
        // lets the driver schedule the next frame.
        std::this_thread::sleep_until(std::chrono::steady_clock::time_point{deadline});
    }
};

/// A clock that only moves when a test moves it.
class ManualClock final : public Clock {
public:
    explicit ManualClock(Nanoseconds start = Nanoseconds{0}) noexcept : now_(start) {}

    [[nodiscard]] Nanoseconds now() const noexcept override { return now_; }

    /// Jumps to the deadline instead of waiting. A test can therefore run a
    /// sixty-second scenario instantly, and can simulate a renderer that misses
    /// its deadline by advancing the clock past it before the next wait.
    void sleep_until(Nanoseconds deadline) override {
        if (deadline > now_) {
            now_ = deadline;
        }
        ++sleeps_;
    }

    void advance(Nanoseconds delta) noexcept { now_ += delta; }
    void set(Nanoseconds value) noexcept { now_ = value; }

    /// How many times the loop waited. A pacer that never sleeps is spinning.
    [[nodiscard]] std::int64_t sleep_count() const noexcept { return sleeps_; }

private:
    Nanoseconds now_;
    std::int64_t sleeps_ = 0;
};

}  // namespace rf::playback

#endif  // RF_PLAYBACK_CLOCK_HPP
