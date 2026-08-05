#include "rf/playback/frame_log.hpp"

#include <gtest/gtest.h>

#include <cstdint>

#include "rf/media/rational.hpp"
#include "rf/playback/clock.hpp"
#include "rf/playback/playback_clock.hpp"

namespace {

using rf::media::Rational;
using rf::playback::FrameLog;
using rf::playback::FrameStatistics;
using rf::playback::ManualClock;
using rf::playback::Nanoseconds;
using rf::playback::PlaybackClock;

constexpr Nanoseconds kZero{0};
constexpr Nanoseconds kFrame30{33'333'333};  // ~1/30 s
constexpr Nanoseconds kLateThreshold{5'000'000};

TEST(FrameLog, StartsEmpty) {
    const FrameLog log;
    EXPECT_EQ(log.presented_count(), 0);
    EXPECT_EQ(log.dropped_count(), 0);
    const FrameStatistics stats = log.statistics(kLateThreshold);
    EXPECT_EQ(stats.interval_p99, kZero);
}

TEST(FrameLog, ConsecutiveFramesDropNothing) {
    FrameLog log;
    Nanoseconds when = kZero;
    for (std::int64_t frame = 0; frame < 100; ++frame) {
        log.record(frame, when, when);
        when += kFrame30;
    }
    EXPECT_EQ(log.presented_count(), 100);
    EXPECT_EQ(log.dropped_count(), 0);
}

TEST(FrameLog, CountsFramesThePlayheadPassedButNeverShowed) {
    FrameLog log;
    log.record(0, kZero, kZero);
    log.record(1, kFrame30, kFrame30);
    // Frames 2, 3 and 4 never reached the screen.
    log.record(5, kFrame30 * 5, kFrame30 * 5);

    EXPECT_EQ(log.presented_count(), 3);
    EXPECT_EQ(log.dropped_count(), 3);
}

TEST(FrameLog, ALateFrameIsNotADroppedFrame) {
    // Presenting the right frame, late, is a pacing problem. Counting it as a
    // drop would conflate two failures that need different fixes.
    FrameLog log;
    log.record(0, kZero, kZero);
    log.record(1, kFrame30 * 3, kFrame30);  // correct frame, two frames late

    EXPECT_EQ(log.dropped_count(), 0);
    const FrameStatistics stats = log.statistics(kLateThreshold);
    EXPECT_EQ(stats.late, 1);
    EXPECT_GT(stats.lateness_max, kFrame30);
}

TEST(FrameLog, AnEarlyFrameIsNotNegativelyLate) {
    // Clamped at zero, so early frames cannot offset late ones in a percentile
    // and quietly flatter the numbers.
    FrameLog log;
    log.record(0, kZero, kZero);
    log.record(1, kFrame30, kFrame30 * 2);  // presented before it was due

    const FrameStatistics stats = log.statistics(kLateThreshold);
    EXPECT_EQ(stats.late, 0);
    EXPECT_EQ(stats.lateness_max, kZero);
}

TEST(FrameLog, RepeatingAFrameDropsNothing) {
    // What happens while paused: the same frame presented again is not the
    // playhead passing anything by.
    FrameLog log;
    log.record(7, kZero, kZero);
    log.record(7, kFrame30, kFrame30);
    log.record(7, kFrame30 * 2, kFrame30 * 2);
    EXPECT_EQ(log.dropped_count(), 0);
    EXPECT_EQ(log.presented_count(), 3);
}

TEST(FrameLog, ASeekIsNotAMassDrop) {
    FrameLog log;
    log.record(0, kZero, kZero);
    log.mark_discontinuity();
    log.record(9000, kFrame30, kFrame30);

    EXPECT_EQ(log.dropped_count(), 0)
        << "frames jumped over by a seek were never due to be shown";
    EXPECT_EQ(log.presented_count(), 2);
}

TEST(FrameLog, BackwardMovementIsNotCountedAsDrops) {
    // Reverse playback without a marked discontinuity must not produce a
    // negative or absurd drop count.
    FrameLog log;
    log.record(100, kZero, kZero);
    log.record(99, kFrame30, kFrame30);
    log.record(98, kFrame30 * 2, kFrame30 * 2);
    EXPECT_EQ(log.dropped_count(), 0);
}

TEST(FrameLog, ReportsIntervalPercentiles) {
    FrameLog log;
    Nanoseconds when = kZero;
    log.record(0, when, when);
    for (std::int64_t frame = 1; frame < 100; ++frame) {
        // One deliberate spike, to prove p99 and max are not just the mean.
        when += (frame == 50) ? kFrame30 * 4 : kFrame30;
        log.record(frame, when, when);
    }

    const FrameStatistics stats = log.statistics(kLateThreshold);
    EXPECT_EQ(stats.presented, 100);
    EXPECT_EQ(stats.dropped, 0);
    EXPECT_EQ(stats.interval_p50, kFrame30);
    EXPECT_EQ(stats.interval_max, kFrame30 * 4);
    EXPECT_GT(stats.interval_max, stats.interval_p50) << "the spike was averaged away";
}

TEST(FrameLog, ClearResetsEverything) {
    FrameLog log;
    log.record(0, kZero, kZero);
    log.record(4, kFrame30, kFrame30);
    ASSERT_GT(log.dropped_count(), 0);

    log.clear();
    EXPECT_EQ(log.presented_count(), 0);
    EXPECT_EQ(log.dropped_count(), 0);
}

// --- the shape of M3's gate --------------------------------------------------

TEST(FrameLog, SixtySecondsOfPerfectPlaybackDropsNothing) {
    // The gate scenario, driven by the real clock arithmetic rather than by
    // hand-written timestamps. A renderer that always hits its due time must
    // produce zero drops across 1,800 frames.
    auto clock = PlaybackClock::create(Rational{30, 1});
    ASSERT_TRUE(clock.has_value());
    ASSERT_TRUE(clock.value().play(kZero, Rational{1, 1}).has_value());

    ManualClock wall;
    FrameLog log(1800);

    for (std::int64_t frame = 0; frame < 1800; ++frame) {
        const auto due = clock.value().time_of_frame(frame);
        ASSERT_TRUE(due.has_value());
        wall.set(due.value());
        log.record(frame, wall.now(), due.value());
    }

    const FrameStatistics stats = log.statistics(kLateThreshold);
    EXPECT_EQ(stats.presented, 1800);
    EXPECT_EQ(stats.dropped, 0);
    EXPECT_EQ(stats.late, 0);
    EXPECT_EQ(stats.lateness_max, kZero);
}

TEST(FrameLog, ARendererThatMissesEveryOtherFrameIsCaught) {
    // The same scenario with a renderer only fast enough for 15 fps. This must
    // fail the gate loudly -- if it did not, the gate would be decorative.
    auto clock = PlaybackClock::create(Rational{30, 1});
    ASSERT_TRUE(clock.has_value());
    ASSERT_TRUE(clock.value().play(kZero, Rational{1, 1}).has_value());

    FrameLog log(900);
    for (std::int64_t frame = 0; frame < 1800; frame += 2) {
        const auto due = clock.value().time_of_frame(frame);
        ASSERT_TRUE(due.has_value());
        log.record(frame, due.value(), due.value());
    }

    const FrameStatistics stats = log.statistics(kLateThreshold);
    EXPECT_EQ(stats.presented, 900);
    EXPECT_EQ(stats.dropped, 899) << "half-rate playback must be reported as dropping frames";
}

}  // namespace
