#include "rf/playback/playback_clock.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>

namespace {

using rf::Errc;
using rf::media::Rational;
using rf::playback::Nanoseconds;
using rf::playback::PlaybackClock;

constexpr Nanoseconds kZero{0};
constexpr Nanoseconds kSecond{1'000'000'000};

PlaybackClock make_clock(const Rational& rate = Rational{30, 1}, std::int64_t start = 0) {
    auto clock = PlaybackClock::create(rate, start);
    EXPECT_TRUE(clock.has_value());
    return std::move(clock).value();
}

TEST(PlaybackClock, RejectsAZeroFrameRate) {
    const auto clock = PlaybackClock::create(Rational{0, 1});
    ASSERT_TRUE(clock.has_error());
    EXPECT_EQ(clock.error().code(), Errc::invalid_argument);
}

TEST(PlaybackClock, RejectsANegativeFrameRate) {
    // Running backwards is a negative play rate, not a negative frame rate.
    // Allowing both would give two spellings of the same state.
    EXPECT_TRUE(PlaybackClock::create(Rational{-30, 1}).has_error());
}

TEST(PlaybackClock, APausedClockDoesNotMove) {
    const PlaybackClock clock = make_clock();
    EXPECT_FALSE(clock.is_playing());
    EXPECT_EQ(clock.frame_at(kZero).value(), 0);
    EXPECT_EQ(clock.frame_at(kSecond * 10).value(), 0);
}

TEST(PlaybackClock, RejectsAZeroPlayRate) {
    PlaybackClock clock = make_clock();
    const auto result = clock.play(kZero, Rational{0, 1});
    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(result.error().code(), Errc::invalid_argument);
}

TEST(PlaybackClock, AdvancesOneFramePerFrameDuration) {
    PlaybackClock clock = make_clock(Rational{30, 1});
    ASSERT_TRUE(clock.play(kZero, Rational{1, 1}).has_value());

    EXPECT_EQ(clock.frame_at(kZero).value(), 0);
    EXPECT_EQ(clock.frame_at(Nanoseconds{33'333'333}).value(), 0) << "just before frame 1 is due";
    EXPECT_EQ(clock.frame_at(Nanoseconds{33'333'334}).value(), 1);
    EXPECT_EQ(clock.frame_at(kSecond).value(), 30);
}

TEST(PlaybackClock, HoldsTheFrameGridExactlyForNtsc) {
    // 30000/1001 is the rate a floating-point playhead drifts off. Sixty
    // seconds in, an accumulating implementation is typically a frame or two
    // out; this must be exact.
    PlaybackClock clock = make_clock(Rational{30000, 1001});
    ASSERT_TRUE(clock.play(kZero, Rational{1, 1}).has_value());

    // Frame N is due at N * 1001/30000 seconds.
    for (const std::int64_t frame : {1, 30, 1000, 1799, 17982}) {
        const auto due = clock.time_of_frame(frame);
        ASSERT_TRUE(due.has_value()) << due.error().to_string();
        EXPECT_EQ(clock.frame_at(due.value()).value(), frame)
            << "frame " << frame << " not recovered from its own due time";
        EXPECT_EQ(clock.frame_at(due.value() - Nanoseconds{1}).value(), frame - 1)
            << "one nanosecond before frame " << frame << " should still be the previous frame";
    }
}

TEST(PlaybackClock, DoesNotDriftOverASustainedRun) {
    // Sixty seconds of 29.97 playback. Position is computed from elapsed time
    // rather than accumulated, so the final frame must be exact rather than
    // approximately right.
    PlaybackClock clock = make_clock(Rational{30000, 1001});
    ASSERT_TRUE(clock.play(kZero, Rational{1, 1}).has_value());

    const auto frame = clock.frame_at(kSecond * 60);
    ASSERT_TRUE(frame.has_value());
    // 60 s / (1001/30000) = 1798.2..., so frame 1798 is on screen.
    EXPECT_EQ(frame.value(), 1798);
}

TEST(PlaybackClock, DoubleRateAdvancesTwiceAsFast) {
    PlaybackClock clock = make_clock(Rational{30, 1});
    ASSERT_TRUE(clock.play(kZero, Rational{2, 1}).has_value());
    EXPECT_EQ(clock.frame_at(kSecond).value(), 60);
}

TEST(PlaybackClock, HalfRateAdvancesHalfAsFast) {
    PlaybackClock clock = make_clock(Rational{30, 1});
    ASSERT_TRUE(clock.play(kZero, Rational{1, 2}).has_value());
    EXPECT_EQ(clock.frame_at(kSecond).value(), 15);
}

TEST(PlaybackClock, NegativeRateRunsBackwards) {
    // JKL shuttle in reverse. Flooring must go toward negative infinity here
    // too, or reverse playback would sit on the wrong frame.
    PlaybackClock clock = make_clock(Rational{30, 1}, 100);
    ASSERT_TRUE(clock.play(kZero, Rational{-1, 1}).has_value());
    EXPECT_EQ(clock.frame_at(kSecond).value(), 70);
}

TEST(PlaybackClock, ChangingRateReanchorsRatherThanRewritingHistory) {
    // Without re-anchoring, switching to 2x after a second of 1x playback would
    // reinterpret that first second as having been at 2x and the playhead would
    // jump.
    PlaybackClock clock = make_clock(Rational{30, 1});
    ASSERT_TRUE(clock.play(kZero, Rational{1, 1}).has_value());
    ASSERT_EQ(clock.frame_at(kSecond).value(), 30);

    ASSERT_TRUE(clock.play(kSecond, Rational{2, 1}).has_value());
    EXPECT_EQ(clock.frame_at(kSecond).value(), 30) << "the playhead jumped when the rate changed";
    EXPECT_EQ(clock.frame_at(kSecond * 2).value(), 90) << "30 + 2x30 for the second second";
}

TEST(PlaybackClock, PauseHoldsThePositionItHadWhenPaused) {
    PlaybackClock clock = make_clock(Rational{30, 1});
    ASSERT_TRUE(clock.play(kZero, Rational{1, 1}).has_value());
    ASSERT_TRUE(clock.pause(kSecond).has_value());

    EXPECT_FALSE(clock.is_playing());
    EXPECT_EQ(clock.frame_at(kSecond).value(), 30);
    EXPECT_EQ(clock.frame_at(kSecond * 100).value(), 30) << "a paused clock kept moving";
}

TEST(PlaybackClock, PausingWhenAlreadyPausedIsANoOp) {
    PlaybackClock clock = make_clock(Rational{30, 1}, 42);
    ASSERT_TRUE(clock.pause(kSecond).has_value());
    EXPECT_EQ(clock.frame_at(kSecond * 5).value(), 42);
}

TEST(PlaybackClock, ResumingContinuesFromWhereItPaused) {
    PlaybackClock clock = make_clock(Rational{30, 1});
    ASSERT_TRUE(clock.play(kZero, Rational{1, 1}).has_value());
    ASSERT_TRUE(clock.pause(kSecond).has_value());

    // A minute of being paused must not advance anything when play resumes.
    ASSERT_TRUE(clock.play(kSecond * 61, Rational{1, 1}).has_value());
    EXPECT_EQ(clock.frame_at(kSecond * 61).value(), 30);
    EXPECT_EQ(clock.frame_at(kSecond * 62).value(), 60);
}

TEST(PlaybackClock, SeekRepositionsAndKeepsPlaying) {
    PlaybackClock clock = make_clock(Rational{30, 1});
    ASSERT_TRUE(clock.play(kZero, Rational{1, 1}).has_value());

    clock.seek(kSecond, 500);
    EXPECT_TRUE(clock.is_playing());
    EXPECT_EQ(clock.frame_at(kSecond).value(), 500);
    EXPECT_EQ(clock.frame_at(kSecond * 2).value(), 530);
}

TEST(PlaybackClock, TimeOfFrameIsTheInverseOfFrameAt) {
    PlaybackClock clock = make_clock(Rational{24, 1});
    ASSERT_TRUE(clock.play(kZero, Rational{1, 1}).has_value());

    for (std::int64_t frame = 0; frame < 200; ++frame) {
        const auto due = clock.time_of_frame(frame);
        ASSERT_TRUE(due.has_value()) << "frame " << frame;
        ASSERT_EQ(clock.frame_at(due.value()).value(), frame) << "frame " << frame;
    }
}

TEST(PlaybackClock, TimeOfFrameIsTheInverseOfFrameAtInReverseToo) {
    // Reverse playback rounds the other way: the position decreases with time,
    // so the instant a frame becomes current is the latest one at or before the
    // exact boundary, not the earliest at or after it.
    PlaybackClock clock = make_clock(Rational{30000, 1001}, 5000);
    ASSERT_TRUE(clock.play(kZero, Rational{-1, 1}).has_value());

    for (std::int64_t frame = 5000; frame > 4900; --frame) {
        const auto due = clock.time_of_frame(frame);
        ASSERT_TRUE(due.has_value()) << "frame " << frame;
        ASSERT_EQ(clock.frame_at(due.value()).value(), frame) << "frame " << frame;
    }
}

TEST(PlaybackClock, TimeOfFrameRoundTripsAtAwkwardRates) {
    // Rates whose frame duration is not a whole number of nanoseconds, which is
    // where a rounding mistake hides.
    for (const Rational rate : {Rational{30000, 1001}, Rational{24000, 1001}, Rational{23, 3},
                                Rational{60000, 1001}, Rational{7, 1}}) {
        PlaybackClock clock = make_clock(rate);
        ASSERT_TRUE(clock.play(kZero, Rational{1, 1}).has_value());
        for (std::int64_t frame = 0; frame < 300; ++frame) {
            const auto due = clock.time_of_frame(frame);
            ASSERT_TRUE(due.has_value()) << rate.to_string() << " frame " << frame;
            ASSERT_EQ(clock.frame_at(due.value()).value(), frame)
                << "rate " << rate.to_string() << ", frame " << frame;
        }
    }
}

TEST(PlaybackClock, APausedClockHasNoDueTimeForOtherFrames) {
    const PlaybackClock clock = make_clock(Rational{30, 1}, 10);
    EXPECT_TRUE(clock.time_of_frame(10).has_value());
    EXPECT_TRUE(clock.time_of_frame(11).has_error())
        << "a paused clock cannot say when a future frame is due";
}

}  // namespace
