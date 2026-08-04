#include "rf/media/rational.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>

namespace {

using rf::Errc;
using rf::media::Rational;
using rf::media::Rounding;
using rf::media::mul_div;
using rf::media::rescale;

constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();

// --- construction and normalisation -----------------------------------------

TEST(Rational, NormalisesByGreatestCommonDivisor) {
    const Rational r{1000, 2000};
    EXPECT_EQ(r.numerator(), 1);
    EXPECT_EQ(r.denominator(), 2);
}

TEST(Rational, MovesSignOntoTheNumerator) {
    const Rational r{1, -3};
    EXPECT_EQ(r.numerator(), -1);
    EXPECT_EQ(r.denominator(), 3);
}

TEST(Rational, NegativeOverNegativeIsPositive) {
    const Rational r{-4, -6};
    EXPECT_EQ(r.numerator(), 2);
    EXPECT_EQ(r.denominator(), 3);
}

TEST(Rational, ZeroIsAlwaysZeroOverOne) {
    EXPECT_EQ(Rational(0, 5), Rational(0, 1));
    EXPECT_EQ(Rational(0, -5).numerator(), 0);
    EXPECT_EQ(Rational(0, -5).denominator(), 1);
    EXPECT_TRUE(Rational(0, -5).is_zero());
}

TEST(Rational, EqualityIsStructuralAfterNormalisation) {
    // The whole point of normalising: two frame rates spelled differently must
    // compare equal, or every "is this 23.976?" check becomes unreliable.
    EXPECT_EQ(Rational(24000, 1001), Rational(48000, 2002));
    EXPECT_EQ(Rational(30000, 1001), Rational(30000, 1001));
    EXPECT_NE(Rational(30000, 1001), Rational(25, 1));
}

TEST(Rational, DefaultIsZero) {
    const Rational r;
    EXPECT_EQ(r.numerator(), 0);
    EXPECT_EQ(r.denominator(), 1);
}

TEST(Rational, FromRejectsZeroDenominator) {
    // Containers really do report 0/0 for an unknown frame rate, so this has to
    // be an error value and not an abort.
    const auto r = Rational::from(0, 0);
    ASSERT_TRUE(r.has_error());
    EXPECT_EQ(r.error().code(), Errc::invalid_argument);
}

TEST(Rational, FromAcceptsExtremeValues) {
    const auto r = Rational::from(kMax, kMax);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), Rational(1, 1));
}

TEST(Rational, FromHandlesInt64MinNumerator) {
    const auto r = Rational::from(kMin, 2);
    ASSERT_TRUE(r.has_value()) << r.error().to_string();
    EXPECT_EQ(r.value().numerator(), kMin / 2);
    EXPECT_EQ(r.value().denominator(), 1);
}

TEST(RationalDeath, ConstructorRejectsZeroDenominator) {
    EXPECT_DEATH((void)Rational(1, 0), "Rational constructed from an invalid pair");
}

// --- ordering ---------------------------------------------------------------

TEST(Rational, OrdersExactlyWithoutOverflow) {
    // Cross-multiplying these in int64 would overflow and give the wrong answer.
    const Rational big_a{kMax, kMax - 1};
    const Rational big_b{kMax - 1, kMax - 2};
    EXPECT_LT(big_a, big_b);
    EXPECT_GT(big_b, big_a);
    EXPECT_FALSE(big_a > big_b);
}

TEST(Rational, OrdersAcrossZero) {
    EXPECT_LT(Rational(-1, 3), Rational(1, 1000000));
    EXPECT_LT(Rational(-1, 1000000), Rational(0, 1));
    EXPECT_LT(Rational(0, 1), Rational(1, 1000000));
}

TEST(Rational, OrdersNegatives) {
    EXPECT_LT(Rational(-2, 3), Rational(-1, 3));
    EXPECT_GT(Rational(-1, 3), Rational(-2, 3));
}

TEST(Rational, ComparesCommonFrameRatesCorrectly) {
    const Rational film{24000, 1001};   // 23.976
    const Rational whole{24, 1};
    const Rational ntsc{30000, 1001};   // 29.97
    EXPECT_LT(film, whole);
    EXPECT_LT(whole, ntsc);
    EXPECT_LE(film, film);
    EXPECT_GE(ntsc, ntsc);
}

// --- arithmetic -------------------------------------------------------------

TEST(Rational, MultipliesAndReduces) {
    const auto r = Rational(2, 3).multiplied_by(Rational(3, 4));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), Rational(1, 2));
}

TEST(Rational, MultipliesLargeValuesByCrossReducing) {
    // Naive multiplication overflows here; cross-reduction makes it exact.
    const auto r = Rational(kMax, 3).multiplied_by(Rational(3, kMax));
    ASSERT_TRUE(r.has_value()) << r.error().to_string();
    EXPECT_EQ(r.value(), Rational(1, 1));
}

TEST(Rational, DividesAndReduces) {
    const auto r = Rational(1, 2).divided_by(Rational(1, 4));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), Rational(2, 1));
}

TEST(Rational, DivisionByZeroIsAnError) {
    const auto r = Rational(1, 2).divided_by(Rational(0, 1));
    ASSERT_TRUE(r.has_error());
    EXPECT_EQ(r.error().code(), Errc::invalid_argument);
}

TEST(Rational, AddsWithUnlikeDenominators) {
    const auto r = Rational(1, 3).plus(Rational(1, 6));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), Rational(1, 2));
}

TEST(Rational, SubtractsToZero) {
    const auto r = Rational(1001, 30000).minus(Rational(1001, 30000));
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r.value().is_zero());
}

TEST(Rational, InvertsAndPreservesSign) {
    const auto r = Rational(-3, 7).inverse();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), Rational(-7, 3));
}

TEST(Rational, InvertingZeroIsAnError) {
    EXPECT_TRUE(Rational(0, 1).inverse().has_error());
}

TEST(Rational, ReportsOverflowRatherThanWrapping) {
    const auto r = Rational(kMax, 1).plus(Rational(kMax, 1));
    ASSERT_TRUE(r.has_error()) << "expected overflow to be reported";
    EXPECT_EQ(r.error().code(), Errc::invalid_argument);
}

TEST(Rational, ToStringRoundTripsThroughFrom) {
    EXPECT_EQ(Rational(24000, 1001).to_string(), "24000/1001");
    EXPECT_EQ(Rational(-1, 2).to_string(), "-1/2");
}

TEST(Rational, ApproximateIsCloseButIsNotUsedForOrdering) {
    EXPECT_NEAR(Rational(24000, 1001).approximate(), 23.976, 0.001);
    EXPECT_NEAR(Rational(1, 3).approximate(), 0.3333333, 1e-6);
}

// --- mul_div ----------------------------------------------------------------

TEST(MulDiv, ExactDivisionIsExact) {
    const auto r = mul_div(90000, 1, 90000, Rounding::nearest);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 1);
}

TEST(MulDiv, UsesA128BitIntermediate) {
    // kMax * 2 does not fit in int64; the intermediate must not wrap.
    const auto r = mul_div(kMax, 2, 2, Rounding::toward_zero);
    ASSERT_TRUE(r.has_value()) << r.error().to_string();
    EXPECT_EQ(r.value(), kMax);
}

TEST(MulDiv, ReportsOverflowWhenTheResultCannotFit) {
    const auto r = mul_div(kMax, 4, 2, Rounding::toward_zero);
    ASSERT_TRUE(r.has_error());
    EXPECT_NE(r.error().message().find("overflow"), std::string::npos);
}

TEST(MulDiv, DivideByZeroIsAnError) {
    EXPECT_TRUE(mul_div(1, 1, 0).has_error());
}

TEST(MulDiv, RoundingDownGoesTowardNegativeInfinity) {
    EXPECT_EQ(mul_div(7, 1, 2, Rounding::down).value(), 3);
    EXPECT_EQ(mul_div(-7, 1, 2, Rounding::down).value(), -4);
}

TEST(MulDiv, RoundingUpGoesTowardPositiveInfinity) {
    EXPECT_EQ(mul_div(7, 1, 2, Rounding::up).value(), 4);
    EXPECT_EQ(mul_div(-7, 1, 2, Rounding::up).value(), -3);
}

TEST(MulDiv, RoundingTowardZeroTruncates) {
    EXPECT_EQ(mul_div(7, 1, 2, Rounding::toward_zero).value(), 3);
    EXPECT_EQ(mul_div(-7, 1, 2, Rounding::toward_zero).value(), -3);
}

TEST(MulDiv, RoundingNearestBreaksHalvesAwayFromZero) {
    EXPECT_EQ(mul_div(5, 1, 2, Rounding::nearest).value(), 3);
    EXPECT_EQ(mul_div(-5, 1, 2, Rounding::nearest).value(), -3);
    EXPECT_EQ(mul_div(4, 1, 2, Rounding::nearest).value(), 2);
    EXPECT_EQ(mul_div(1, 1, 3, Rounding::nearest).value(), 0);
    EXPECT_EQ(mul_div(2, 1, 3, Rounding::nearest).value(), 1);
}

TEST(MulDiv, SignsCombineCorrectly) {
    EXPECT_EQ(mul_div(-6, 2, 3, Rounding::toward_zero).value(), -4);
    EXPECT_EQ(mul_div(6, -2, 3, Rounding::toward_zero).value(), -4);
    EXPECT_EQ(mul_div(6, 2, -3, Rounding::toward_zero).value(), -4);
    EXPECT_EQ(mul_div(-6, -2, -3, Rounding::toward_zero).value(), -4);
    EXPECT_EQ(mul_div(-6, -2, 3, Rounding::toward_zero).value(), 4);
}

// --- rescale ----------------------------------------------------------------

TEST(Rescale, ConvertsBetweenTimeBases) {
    // 90000 ticks at 1/90000 is one second, which is 1000 ms.
    const auto r = rescale(90000, Rational(1, 90000), Rational(1, 1000));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 1000);
}

TEST(Rescale, IsIdentityForTheSameBase) {
    const auto r = rescale(123456789, Rational(1, 90000), Rational(1, 90000));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 123456789);
}

TEST(Rescale, HandlesNtscFrameDurationsExactly) {
    // One 29.97 fps frame is 1001/30000 s, which is exactly 3003 ticks of a
    // 1/90000 base. This is the conversion a float pipeline gets wrong.
    const auto r = rescale(1, Rational(1001, 30000), Rational(1, 90000));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 3003);
}

TEST(Rescale, TenMinutesOf4kAtNtscDoesNotDrift) {
    // The M1 gate is a 10-minute file. 29.97 fps for 10 minutes is 17982 frames;
    // converting the last frame's index to a 90 kHz base and back must land on
    // exactly that frame, not one either side.
    constexpr std::int64_t kLastFrame = 17981;
    const Rational frame_duration{1001, 30000};
    const Rational tick{1, 90000};

    const auto ticks = rescale(kLastFrame, frame_duration, tick, Rounding::nearest);
    ASSERT_TRUE(ticks.has_value());
    EXPECT_EQ(ticks.value(), kLastFrame * 3003);

    const auto back = rescale(ticks.value(), tick, frame_duration, Rounding::nearest);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back.value(), kLastFrame) << "round trip drifted";
}

TEST(Rescale, RoundTripsEveryFrameOfATenMinuteNtscTimeline) {
    // Exhaustive rather than sampled: ±1 drift bugs hide in specific residues,
    // and 18k iterations is cheap.
    const Rational frame_duration{1001, 30000};
    const Rational tick{1, 90000};
    for (std::int64_t frame = 0; frame < 17982; ++frame) {
        const auto ticks = rescale(frame, frame_duration, tick, Rounding::nearest);
        ASSERT_TRUE(ticks.has_value()) << "frame " << frame;
        const auto back = rescale(ticks.value(), tick, frame_duration, Rounding::nearest);
        ASSERT_TRUE(back.has_value()) << "frame " << frame;
        ASSERT_EQ(back.value(), frame) << "drift at frame " << frame;
    }
}

TEST(Rescale, RejectsAZeroTargetBase) {
    const auto r = rescale(1, Rational(1, 30), Rational(0, 1));
    ASSERT_TRUE(r.has_error());
    EXPECT_EQ(r.error().code(), Errc::invalid_argument);
}

TEST(Rescale, ZeroSourceBaseYieldsZero) {
    const auto r = rescale(12345, Rational(0, 1), Rational(1, 90000));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 0);
}

TEST(Rescale, SurvivesALongTimelineInAHighResolutionBase) {
    // Three hours in a 1/1000000 base, rescaled to 1/90000: the intermediate
    // product is well past 64 bits.
    constexpr std::int64_t kThreeHoursMicros = 3LL * 3600 * 1000 * 1000;
    const auto r = rescale(kThreeHoursMicros, Rational(1, 1000000), Rational(1, 90000));
    ASSERT_TRUE(r.has_value()) << r.error().to_string();
    EXPECT_EQ(r.value(), 3LL * 3600 * 90000);
}

}  // namespace
