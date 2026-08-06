// JKL, checked against the definitions in docs/adr/014-jkl-shuttle.md.
//
// The rate is a signed rational, so these read as exact values rather than as
// floating-point comparisons: half speed is 1/2, and reverse double speed is
// -2/1, not -1.9999999.

#include "rf/edit/shuttle.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

using rf::edit::Shuttle;
using rf::media::Rational;

Rational rate_after(std::initializer_list<char> keys) {
    Shuttle shuttle;
    for (const char key : keys) {
        switch (key) {
            case 'L': shuttle.forward(); break;
            case 'J': shuttle.backward(); break;
            case 'K': shuttle.stop(); break;
            case 'l': shuttle.slow_forward(); break;
            case 'j': shuttle.slow_backward(); break;
            default: break;
        }
    }
    return shuttle.rate();
}

TEST(ShuttleTest, StartsStopped) {
    const Shuttle shuttle;
    EXPECT_TRUE(shuttle.is_stopped());
    EXPECT_EQ(shuttle.rate(), (Rational{0, 1}));
}

TEST(ShuttleTest, LClimbsTheLadderAndStopsAtTheTop) {
    EXPECT_EQ(rate_after({'L'}), (Rational{1, 1}));
    EXPECT_EQ(rate_after({'L', 'L'}), (Rational{2, 1}));
    EXPECT_EQ(rate_after({'L', 'L', 'L'}), (Rational{4, 1}));
    EXPECT_EQ(rate_after({'L', 'L', 'L', 'L'}), (Rational{8, 1}));
    EXPECT_EQ(rate_after({'L', 'L', 'L', 'L', 'L'}), (Rational{16, 1}));
    EXPECT_EQ(rate_after({'L', 'L', 'L', 'L', 'L', 'L', 'L'}), (Rational{16, 1}))
        << "the top rung is the top; further presses must not run away";
}

TEST(ShuttleTest, JIsTheMirrorOfL) {
    EXPECT_EQ(rate_after({'J'}), (Rational{-1, 1}));
    EXPECT_EQ(rate_after({'J', 'J'}), (Rational{-2, 1}));
    EXPECT_EQ(rate_after({'J', 'J', 'J', 'J', 'J', 'J'}), (Rational{-16, 1}));
}

TEST(ShuttleTest, TheOppositeKeyStepsOneRungRatherThanReversing) {
    // ADR 014 decision 2. A single keystroke that flips from 8x forward to
    // reverse would be hard to undo by feel, and this is a control operated
    // without looking.
    EXPECT_EQ(rate_after({'L', 'L', 'L', 'J'}), (Rational{2, 1}));
    EXPECT_EQ(rate_after({'L', 'L', 'L', 'J', 'J'}), (Rational{1, 1}));
    EXPECT_EQ(rate_after({'L', 'L', 'L', 'J', 'J', 'J'}), (Rational{0, 1}))
        << "1x plus the opposite key is a stop";
    EXPECT_EQ(rate_after({'L', 'L', 'L', 'J', 'J', 'J', 'J'}), (Rational{-1, 1}))
        << "and only then does it reverse";
}

TEST(ShuttleTest, KStopsFromAnySpeedAndEitherDirection) {
    EXPECT_EQ(rate_after({'L', 'L', 'L', 'L', 'K'}), (Rational{0, 1}));
    EXPECT_EQ(rate_after({'J', 'J', 'J', 'K'}), (Rational{0, 1}));
    EXPECT_EQ(rate_after({'K'}), (Rational{0, 1})) << "stopping while stopped is not an error";
}

TEST(ShuttleTest, StoppingResetsTheLadderRatherThanRemembering) {
    // Otherwise K then L resumes at whatever speed the shuttle happened to be
    // doing, which is a surprise on a control used to regain composure.
    EXPECT_EQ(rate_after({'L', 'L', 'L', 'K', 'L'}), (Rational{1, 1}));
}

TEST(ShuttleTest, ShiftGivesExactHalfSpeed) {
    EXPECT_EQ(rate_after({'l'}), (Rational{1, 2}));
    EXPECT_EQ(rate_after({'j'}), (Rational{-1, 2}));
}

TEST(ShuttleTest, LeavingSlowMotionRejoinsTheLadderAtOne) {
    EXPECT_EQ(rate_after({'l', 'L'}), (Rational{1, 1}));
    EXPECT_EQ(rate_after({'j', 'J'}), (Rational{-1, 1}));
    EXPECT_EQ(rate_after({'l', 'J'}), (Rational{-1, 1}))
        << "the opposite key from slow motion changes direction, not a rung";
    EXPECT_EQ(rate_after({'l', 'K'}), (Rational{0, 1}));
}

TEST(ShuttleTest, SlowMotionCanBeEnteredFromSpeed) {
    EXPECT_EQ(rate_after({'L', 'L', 'L', 'l'}), (Rational{1, 2}));
}

TEST(ShuttleTest, TheLadderIsDataAndCanBeReplaced) {
    // The sources disagreed about doubling versus linear (ADR 014), so the
    // ladder is configuration rather than a constant in the transition logic.
    Shuttle linear{{Rational{1, 1}, Rational{2, 1}, Rational{3, 1}}};
    linear.forward();
    linear.forward();
    linear.forward();
    EXPECT_EQ(linear.rate(), (Rational{3, 1}));
    linear.forward();
    EXPECT_EQ(linear.rate(), (Rational{3, 1})) << "top of a three-rung ladder";
}

TEST(ShuttleTest, ASingleRungLadderStillShuttles) {
    Shuttle one{{Rational{1, 1}}};
    one.forward();
    EXPECT_EQ(one.rate(), (Rational{1, 1}));
    one.forward();
    EXPECT_EQ(one.rate(), (Rational{1, 1}));
    one.backward();
    EXPECT_EQ(one.rate(), (Rational{0, 1}));
}

TEST(ShuttleTest, IsStoppedAgreesWithARateOfZero) {
    Shuttle shuttle;
    EXPECT_TRUE(shuttle.is_stopped());
    shuttle.forward();
    EXPECT_FALSE(shuttle.is_stopped());
    shuttle.slow_backward();
    EXPECT_FALSE(shuttle.is_stopped()) << "slow motion is still motion";
    shuttle.stop();
    EXPECT_TRUE(shuttle.is_stopped());
}

}  // namespace
