// The join between the shuttle and the clock, driven by an explicit `now`
// rather than by waiting. See docs/adr/014-jkl-shuttle.md.

#include "rf/app/transport.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace {

using rf::app::Transport;
using rf::edit::Shuttle;
using rf::media::Rational;
using rf::playback::Nanoseconds;

constexpr Nanoseconds second(std::int64_t count) {
    return std::chrono::duration_cast<Nanoseconds>(std::chrono::seconds{count});
}

Transport make_transport() {
    auto transport = Transport::create(Rational{30, 1});
    EXPECT_TRUE(transport.has_value());
    return std::move(transport).value();
}

TEST(TransportTest, AStoppedShuttlePausesRatherThanPlayingAtZero) {
    // PlaybackClock rejects a zero rate deliberately, so "playing at zero speed"
    // cannot become a state to reason about. Handing a stopped shuttle straight
    // through as a rate would hit that rejection on every K press.
    Transport transport = make_transport();
    const Shuttle stopped;
    ASSERT_TRUE(transport.apply(stopped, second(0)).has_value());
    EXPECT_FALSE(transport.is_playing());
}

TEST(TransportTest, LPlaysForwardAtOneFramePerFrameOfWallTime) {
    Transport transport = make_transport();
    Shuttle shuttle;
    shuttle.forward();
    ASSERT_TRUE(transport.apply(shuttle, second(0)).has_value());

    EXPECT_TRUE(transport.is_playing());
    EXPECT_EQ(transport.frame_at(second(0)).value(), 0);
    EXPECT_EQ(transport.frame_at(second(1)).value(), 30) << "30 fps, one second";
    EXPECT_EQ(transport.frame_at(second(10)).value(), 300);
}

TEST(TransportTest, ShuttlingFasterMovesTheClockFaster) {
    Transport transport = make_transport();
    Shuttle shuttle;
    shuttle.forward();
    shuttle.forward();  // 2x
    ASSERT_TRUE(transport.apply(shuttle, second(0)).has_value());
    EXPECT_EQ(transport.rate(), (Rational{2, 1}));
    EXPECT_EQ(transport.frame_at(second(1)).value(), 60);
}

TEST(TransportTest, JRunsTheClockBackwards) {
    Transport transport = make_transport();
    transport.seek(second(0), 300);

    Shuttle shuttle;
    shuttle.backward();
    ASSERT_TRUE(transport.apply(shuttle, second(0)).has_value());
    EXPECT_EQ(transport.frame_at(second(1)).value(), 270);
    EXPECT_EQ(transport.frame_at(second(5)).value(), 150);
}

TEST(TransportTest, SlowMotionIsExactlyHalfSpeed) {
    Transport transport = make_transport();
    Shuttle shuttle;
    shuttle.slow_forward();
    ASSERT_TRUE(transport.apply(shuttle, second(0)).has_value());
    EXPECT_EQ(transport.rate(), (Rational{1, 2}));
    EXPECT_EQ(transport.frame_at(second(2)).value(), 30) << "two seconds of wall time, one of edit";
}

TEST(TransportTest, ChangingRateMidPlaybackKeepsThePlayheadWhereItWas) {
    // Re-anchoring is what stops a rate change from also being a jump. Without
    // it, pressing L a second time would teleport the playhead.
    Transport transport = make_transport();
    Shuttle shuttle;
    shuttle.forward();
    ASSERT_TRUE(transport.apply(shuttle, second(0)).has_value());
    ASSERT_EQ(transport.frame_at(second(2)).value(), 60);

    shuttle.forward();  // 2x, two seconds in
    ASSERT_TRUE(transport.apply(shuttle, second(2)).has_value());
    EXPECT_EQ(transport.frame_at(second(2)).value(), 60) << "no jump at the moment of the change";
    EXPECT_EQ(transport.frame_at(second(3)).value(), 120) << "and twice as fast afterwards";
}

TEST(TransportTest, StoppingLeavesThePlayheadWhereItWas) {
    Transport transport = make_transport();
    Shuttle shuttle;
    shuttle.forward();
    ASSERT_TRUE(transport.apply(shuttle, second(0)).has_value());

    shuttle.stop();
    ASSERT_TRUE(transport.apply(shuttle, second(4)).has_value());
    EXPECT_FALSE(transport.is_playing());
    EXPECT_EQ(transport.frame_at(second(4)).value(), 120);
    EXPECT_EQ(transport.frame_at(second(99)).value(), 120) << "a stopped playhead does not drift";
}

TEST(TransportTest, StoppingWhileAlreadyStoppedIsNotAnError) {
    Transport transport = make_transport();
    const Shuttle stopped;
    ASSERT_TRUE(transport.apply(stopped, second(0)).has_value());
    EXPECT_TRUE(transport.apply(stopped, second(1)).has_value());
    EXPECT_EQ(transport.frame_at(second(5)).value(), 0);
}

TEST(TransportTest, RefusesAFrameRateThatIsNotPlayable) {
    EXPECT_TRUE(Transport::create(Rational{0, 1}).has_error());
}

}  // namespace
