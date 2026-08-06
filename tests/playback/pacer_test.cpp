#include "rf/playback/pacer.hpp"

#include <gtest/gtest.h>

#include <cstdint>

#include "rf/media/rational.hpp"
#include "rf/playback/clock.hpp"
#include "rf/playback/frame_log.hpp"
#include "rf/playback/playback_clock.hpp"

namespace {

using rf::media::Rational;
using rf::playback::FrameLog;
using rf::playback::FrameStatistics;
using rf::playback::ManualClock;
using rf::playback::Nanoseconds;
using rf::playback::Pacer;
using rf::playback::PlaybackClock;

constexpr Nanoseconds kZero{0};
constexpr Nanoseconds kFrame30{33'333'334};  // one frame at 30 fps, rounded up

struct Harness {
    ManualClock wall{kZero};
    PlaybackClock playback = PlaybackClock::create(Rational{30, 1}).value();
    FrameLog log;
    Pacer pacer{wall, playback, log};

    Harness() { EXPECT_TRUE(playback.play(kZero, Rational{1, 1}).has_value()); }
};

TEST(Pacer, RefusesToWaitWhilePaused) {
    // Returning the current frame forever would turn a stopped editor into a
    // busy loop.
    ManualClock wall{kZero};
    auto playback = PlaybackClock::create(Rational{30, 1});
    ASSERT_TRUE(playback.has_value());
    FrameLog log;
    Pacer pacer{wall, playback.value(), log};

    const auto tick = pacer.wait_next();
    ASSERT_TRUE(tick.has_error());
    EXPECT_EQ(tick.error().code(), rf::Errc::invalid_argument);
}

TEST(Pacer, ActuallyWaits) {
    // A pacer that never sleeps is spinning, which would burn a core and blow
    // the idle-CPU budget.
    Harness h;
    const auto tick = h.pacer.wait_next();
    ASSERT_TRUE(tick.has_value()) << tick.error().to_string();
    EXPECT_GT(h.wall.sleep_count(), 0);
    EXPECT_GE(h.wall.now(), tick.value().due);
}

TEST(Pacer, DeliversConsecutiveFramesWhenRenderingKeepsUp) {
    Harness h;
    for (std::int64_t expected = 0; expected < 120; ++expected) {
        const auto tick = h.pacer.wait_next();
        ASSERT_TRUE(tick.has_value()) << tick.error().to_string();
        ASSERT_EQ(tick.value().frame, expected);
        // Rendering costs nothing in this scenario.
        h.pacer.presented(tick.value(), h.wall.now());
    }
    EXPECT_EQ(h.log.dropped_count(), 0);
    EXPECT_EQ(h.log.presented_count(), 120);
}

TEST(Pacer, SkipsAheadWhenRenderingFallsBehind) {
    // A renderer taking three frame periods must not fall one frame further
    // behind on every frame; it has to skip to what should be on screen.
    Harness h;
    std::int64_t previous = -1;
    for (int i = 0; i < 20; ++i) {
        const auto tick = h.pacer.wait_next();
        ASSERT_TRUE(tick.has_value()) << tick.error().to_string();
        ASSERT_GT(tick.value().frame, previous) << "the pacer went backwards";
        previous = tick.value().frame;

        // Simulate a slow render: three frame periods of work.
        h.wall.advance(kFrame30 * 3);
        h.pacer.presented(tick.value(), h.wall.now());
    }

    EXPECT_GT(h.log.dropped_count(), 0) << "a renderer at a third of the rate dropped nothing";
    // Twenty presentations spanning roughly sixty frame periods.
    EXPECT_GE(previous, 50) << "the pacer did not keep up with the clock";
}

TEST(Pacer, LatenessIsRecordedSeparatelyFromDrops) {
    // Finishing late but on the right frame is a pacing problem, not a drop.
    Harness h;
    for (int i = 0; i < 10; ++i) {
        const auto tick = h.pacer.wait_next();
        ASSERT_TRUE(tick.has_value()) << tick.error().to_string();
        // A little over a third of a frame late: never enough to skip a frame.
        h.wall.advance(Nanoseconds{12'000'000});
        h.pacer.presented(tick.value(), h.wall.now());
    }

    EXPECT_EQ(h.log.dropped_count(), 0) << "late frames were miscounted as drops";
    const FrameStatistics stats = h.log.statistics(Nanoseconds{5'000'000});
    EXPECT_GT(stats.late, 0) << "lateness was not recorded at all";
}

TEST(Pacer, ResynchroniseRereadsTheClockAfterASeek) {
    Harness h;
    const auto first = h.pacer.wait_next();
    ASSERT_TRUE(first.has_value());
    h.pacer.presented(first.value(), h.wall.now());

    h.playback.seek(h.wall.now(), 9000);
    h.pacer.resynchronise();
    h.log.mark_discontinuity();

    const auto after = h.pacer.wait_next();
    ASSERT_TRUE(after.has_value()) << after.error().to_string();
    EXPECT_GE(after.value().frame, 9000) << "the pacer ignored the seek";
    h.pacer.presented(after.value(), h.wall.now());
    EXPECT_EQ(h.log.dropped_count(), 0) << "a seek was counted as thousands of drops";
}

TEST(Pacer, SixtySecondsOfPacedPlaybackDropsNothing) {
    // The gate scenario, actually paced -- the pacer waits for each frame and
    // the renderer costs nothing. Runs instantly because ManualClock jumps to
    // the deadline instead of sleeping.
    Harness h;
    constexpr std::int64_t kFrames = 1800;

    for (std::int64_t i = 0; i < kFrames; ++i) {
        const auto tick = h.pacer.wait_next();
        ASSERT_TRUE(tick.has_value()) << tick.error().to_string();
        ASSERT_EQ(tick.value().frame, i);
        h.pacer.presented(tick.value(), h.wall.now());
    }

    const FrameStatistics stats = h.log.statistics(Nanoseconds{5'000'000});
    EXPECT_EQ(stats.presented, kFrames);
    EXPECT_EQ(stats.dropped, 0);
    EXPECT_EQ(stats.late, 0);
    EXPECT_EQ(h.wall.sleep_count(), kFrames) << "the loop skipped a wait somewhere";
}

TEST(Pacer, ARendererAtHalfRateIsCaught) {
    // The inverse check: if this did not report drops, the paced gate would be
    // as decorative as the unpaced one.
    Harness h;
    for (int i = 0; i < 100; ++i) {
        const auto tick = h.pacer.wait_next();
        ASSERT_TRUE(tick.has_value()) << tick.error().to_string();
        h.wall.advance(kFrame30 * 2);
        h.pacer.presented(tick.value(), h.wall.now());
    }
    EXPECT_GE(h.log.dropped_count(), 90)
        << "a renderer managing half the frame rate should drop about one frame per frame";
}

TEST(Pacer, HandlesNonIntegerFrameRates) {
    ManualClock wall{kZero};
    auto playback = PlaybackClock::create(Rational{30000, 1001});
    ASSERT_TRUE(playback.has_value());
    ASSERT_TRUE(playback.value().play(kZero, Rational{1, 1}).has_value());
    FrameLog log;
    Pacer pacer{wall, playback.value(), log};

    for (std::int64_t expected = 0; expected < 300; ++expected) {
        const auto tick = pacer.wait_next();
        ASSERT_TRUE(tick.has_value()) << tick.error().to_string();
        ASSERT_EQ(tick.value().frame, expected) << "drifted at frame " << expected;
        pacer.presented(tick.value(), wall.now());
    }
    EXPECT_EQ(log.dropped_count(), 0);
}

}  // namespace
