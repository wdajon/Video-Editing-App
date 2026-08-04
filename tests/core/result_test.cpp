#include "rf/core/result.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using rf::Errc;
using rf::Error;
using rf::Result;

Error make_error(std::string message = "boom") {
    return Error{Errc::decode_failure, std::move(message)};
}

TEST(Result, HoldsValueOnSuccess) {
    Result<int> r = 42;
    EXPECT_TRUE(r.has_value());
    EXPECT_FALSE(r.has_error());
    EXPECT_TRUE(static_cast<bool>(r));
    EXPECT_EQ(r.value(), 42);
    EXPECT_EQ(*r, 42);
}

TEST(Result, HoldsErrorOnFailure) {
    Result<int> r = make_error("no keyframe");
    EXPECT_FALSE(r.has_value());
    EXPECT_TRUE(r.has_error());
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_EQ(r.error().code(), Errc::decode_failure);
    EXPECT_EQ(r.error().message(), "no keyframe");
}

TEST(Result, MoveOnlyValueRoundTrips) {
    Result<std::unique_ptr<int>> r = std::make_unique<int>(7);
    ASSERT_TRUE(r.has_value());
    std::unique_ptr<int> taken = std::move(r).value();
    ASSERT_NE(taken, nullptr);
    EXPECT_EQ(*taken, 7);
}

TEST(Result, ArrowOperatorReachesValueMembers) {
    Result<std::string> r = std::string{"reel"};
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 4u);
}

TEST(Result, DefaultConstructedIsSuccess) {
    Result<int> r;
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 0);
}

TEST(Result, ValueOrReturnsFallbackOnError) {
    Result<int> good = 5;
    Result<int> bad = make_error();
    EXPECT_EQ(good.value_or(99), 5);
    EXPECT_EQ(bad.value_or(99), 99);
}

TEST(Result, ValueOrDoesNotConsumeSuccessfulRvalue) {
    Result<std::string> r = std::string{"kept"};
    EXPECT_EQ(std::move(r).value_or("fallback"), "kept");
}

TEST(Result, EqualityComparesTagAndPayload) {
    EXPECT_EQ(Result<int>(1), Result<int>(1));
    EXPECT_NE(Result<int>(1), Result<int>(2));
    EXPECT_NE(Result<int>(1), Result<int>(make_error()));
    EXPECT_EQ(Result<int>(make_error("a")), Result<int>(make_error("a")));
    EXPECT_NE(Result<int>(make_error("a")), Result<int>(make_error("b")));
}

// --- void ------------------------------------------------------------------

TEST(ResultVoid, DefaultIsSuccess) {
    Result<void> r;
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(static_cast<bool>(r));
}

TEST(ResultVoid, OkHelperIsSuccess) {
    EXPECT_TRUE(rf::ok().has_value());
}

TEST(ResultVoid, CarriesError) {
    Result<void> r = make_error("mux failed");
    EXPECT_TRUE(r.has_error());
    EXPECT_EQ(r.error().message(), "mux failed");
}

// --- and_then ---------------------------------------------------------------

TEST(ResultAndThen, ChainsOnSuccess) {
    Result<int> r = 21;
    Result<int> doubled = std::move(r).and_then([](int v) -> Result<int> { return v * 2; });
    ASSERT_TRUE(doubled.has_value());
    EXPECT_EQ(doubled.value(), 42);
}

TEST(ResultAndThen, ShortCircuitsOnError) {
    bool called = false;
    Result<int> r = make_error("first");
    Result<int> chained = std::move(r).and_then([&](int v) -> Result<int> {
        called = true;
        return v;
    });
    EXPECT_FALSE(called);
    ASSERT_TRUE(chained.has_error());
    EXPECT_EQ(chained.error().message(), "first");
}

TEST(ResultAndThen, PropagatesErrorRaisedByContinuation) {
    Result<int> r = 1;
    Result<int> chained =
        std::move(r).and_then([](int) -> Result<int> { return make_error("second"); });
    ASSERT_TRUE(chained.has_error());
    EXPECT_EQ(chained.error().message(), "second");
}

TEST(ResultAndThen, ChangesValueType) {
    Result<int> r = 3;
    Result<std::string> mapped =
        std::move(r).and_then([](int v) -> Result<std::string> { return std::to_string(v); });
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped.value(), "3");
}

TEST(ResultAndThen, WorksOnConstLvalue) {
    const Result<int> r = 4;
    Result<int> chained = r.and_then([](int v) -> Result<int> { return v + 1; });
    ASSERT_TRUE(chained.has_value());
    EXPECT_EQ(chained.value(), 5);
    EXPECT_EQ(r.value(), 4);
}

TEST(ResultAndThen, VoidSourceInvokesWithoutArgument) {
    Result<void> r;
    Result<int> chained = std::move(r).and_then([]() -> Result<int> { return 8; });
    ASSERT_TRUE(chained.has_value());
    EXPECT_EQ(chained.value(), 8);
}

// --- map --------------------------------------------------------------------

TEST(ResultMap, TransformsValue) {
    Result<int> r = 10;
    Result<std::string> mapped = std::move(r).map([](int v) { return std::to_string(v); });
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped.value(), "10");
}

TEST(ResultMap, LeavesErrorUntouched) {
    Result<int> r = make_error("decode");
    Result<std::string> mapped = std::move(r).map([](int v) { return std::to_string(v); });
    ASSERT_TRUE(mapped.has_error());
    EXPECT_EQ(mapped.error().code(), Errc::decode_failure);
}

TEST(ResultMap, VoidReturningCallableProducesResultVoid) {
    int side_effect = 0;
    Result<int> r = 5;
    Result<void> mapped = std::move(r).map([&](int v) { side_effect = v; });
    EXPECT_TRUE(mapped.has_value());
    EXPECT_EQ(side_effect, 5);
}

TEST(ResultMap, VoidReturningCallableSkippedOnError) {
    int side_effect = 0;
    Result<int> r = make_error();
    Result<void> mapped = std::move(r).map([&](int v) { side_effect = v; });
    EXPECT_TRUE(mapped.has_error());
    EXPECT_EQ(side_effect, 0);
}

TEST(ResultMap, WorksOnConstLvalue) {
    const Result<int> r = 6;
    Result<int> mapped = r.map([](int v) { return v * 3; });
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped.value(), 18);
}

// --- map_error --------------------------------------------------------------

TEST(ResultMapError, RewritesError) {
    Result<int> r = make_error("raw");
    Result<int> remapped =
        std::move(r).map_error([](Error e) { return e.with_context("export"); });
    ASSERT_TRUE(remapped.has_error());
    EXPECT_EQ(remapped.error().message(), "export: raw");
    EXPECT_EQ(remapped.error().code(), Errc::decode_failure);
}

TEST(ResultMapError, LeavesValueUntouched) {
    Result<int> r = 12;
    Result<int> remapped =
        std::move(r).map_error([](Error e) { return e.with_context("unused"); });
    ASSERT_TRUE(remapped.has_value());
    EXPECT_EQ(remapped.value(), 12);
}

TEST(ResultMapError, PreservesVoidSuccess) {
    Result<void> r;
    Result<void> remapped = std::move(r).map_error([](Error e) { return e; });
    EXPECT_TRUE(remapped.has_value());
}

TEST(ResultMapError, ChangesErrorType) {
    Result<int> r = make_error("original");
    Result<int, std::string> remapped =
        std::move(r).map_error([](Error e) { return e.message(); });
    ASSERT_TRUE(remapped.has_error());
    EXPECT_EQ(remapped.error(), "original");
}

// --- misuse -----------------------------------------------------------------
// A failed Result must never hand back a plausible-looking default value: that
// is how a decode failure turns into a silently black frame in an export.

TEST(ResultDeath, ValueOnErrorAborts) {
    EXPECT_DEATH(
        {
            Result<int> r = make_error();
            (void)r.value();
        },
        "Result::value\\(\\) called on a failed Result");
}

TEST(ResultDeath, ErrorOnSuccessAborts) {
    EXPECT_DEATH(
        {
            Result<int> r = 1;
            (void)r.error();
        },
        "Result::error\\(\\) called on a successful Result");
}

TEST(ResultDeath, ArrowOnErrorAborts) {
    EXPECT_DEATH(
        {
            Result<std::string> r = make_error();
            (void)r->size();
        },
        "Result::operator-> called on a failed Result");
}

}  // namespace
