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
};

/// A clock that only moves when a test moves it.
class ManualClock final : public Clock {
public:
    explicit ManualClock(Nanoseconds start = Nanoseconds{0}) noexcept : now_(start) {}

    [[nodiscard]] Nanoseconds now() const noexcept override { return now_; }

    void advance(Nanoseconds delta) noexcept { now_ += delta; }
    void set(Nanoseconds value) noexcept { now_ = value; }

private:
    Nanoseconds now_;
};

}  // namespace rf::playback

#endif  // RF_PLAYBACK_CLOCK_HPP
