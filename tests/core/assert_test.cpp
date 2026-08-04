#include "rf/core/assert.hpp"

#include <gtest/gtest.h>

namespace {

TEST(Check, PassesWithoutSideEffects) {
    int evaluations = 0;
    const auto truthy = [&] {
        ++evaluations;
        return true;
    };
    RF_CHECK(truthy());
    EXPECT_EQ(evaluations, 1) << "RF_CHECK must evaluate its expression exactly once";
}

TEST(Check, EvaluatesExpressionExactlyOnceOnSuccessWithMessage) {
    int evaluations = 0;
    const auto truthy = [&] {
        ++evaluations;
        return true;
    };
    RF_CHECK_MSG(truthy(), "unused");
    EXPECT_EQ(evaluations, 1);
}

TEST(CheckDeath, FailureAbortsAndReportsExpression) {
    EXPECT_DEATH(
        {
            const int frames = 0;
            RF_CHECK(frames > 0);
        },
        "check failed: frames > 0");
}

TEST(CheckDeath, FailureReportsMessage) {
    EXPECT_DEATH(RF_CHECK_MSG(false, "timeline and render graph disagree"),
                 "timeline and render graph disagree");
}

TEST(CheckDeath, FailureReportsSourceLocation) {
    EXPECT_DEATH(RF_CHECK(false), "assert_test.cpp");
}

// RF_CHECK is active in release builds too: an invariant violation that only
// aborts in debug is an invariant violation that ships.
TEST(CheckDeath, IsActiveInEveryConfiguration) {
#ifdef NDEBUG
    EXPECT_DEATH(RF_CHECK(false), "ReelForge fatal");
#else
    EXPECT_DEATH(RF_CHECK(false), "ReelForge fatal");
#endif
}

}  // namespace
