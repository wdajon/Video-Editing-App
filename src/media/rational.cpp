#include "rf/media/rational.hpp"

#include <limits>
#include <string>

#include "rf/core/assert.hpp"

namespace rf::media {
namespace {

constexpr std::int64_t kInt64Max = std::numeric_limits<std::int64_t>::max();
constexpr std::uint64_t kInt64MaxMagnitude = static_cast<std::uint64_t>(kInt64Max);
constexpr std::uint64_t kInt64MinMagnitude = kInt64MaxMagnitude + 1u;
constexpr std::uint64_t kLowMask = 0xFFFF'FFFFull;

/// Magnitude of a signed value, correct for INT64_MIN (whose negation is not
/// representable as int64 -- the classic hole in `-v`).
constexpr std::uint64_t magnitude(std::int64_t v) noexcept {
    return v < 0 ? (~static_cast<std::uint64_t>(v) + 1u) : static_cast<std::uint64_t>(v);
}

struct U128 {
    std::uint64_t hi;
    std::uint64_t lo;
};

/// Full 64x64 -> 128 bit product, assembled from 32-bit limbs.
///
/// Written in portable C++ rather than with `_umul128` or `unsigned __int128`
/// so there is one implementation to reason about and test on every target,
/// instead of a platform split in the code every timestamp flows through. This
/// runs at frame rate, not pixel rate, so the cost of doing it by hand is noise.
constexpr U128 multiply(std::uint64_t a, std::uint64_t b) noexcept {
    const std::uint64_t a_lo = a & kLowMask;
    const std::uint64_t a_hi = a >> 32;
    const std::uint64_t b_lo = b & kLowMask;
    const std::uint64_t b_hi = b >> 32;

    const std::uint64_t ll = a_lo * b_lo;
    const std::uint64_t lh = a_lo * b_hi;
    const std::uint64_t hl = a_hi * b_lo;
    const std::uint64_t hh = a_hi * b_hi;

    const std::uint64_t mid = (ll >> 32) + (lh & kLowMask) + (hl & kLowMask);

    U128 out{};
    out.lo = (ll & kLowMask) | (mid << 32);
    out.hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
    return out;
}

/// 128/64 division by restoring long division. Returns false when the quotient
/// would not fit in 64 bits, which is the overflow case callers must surface.
constexpr bool divide(U128 numerator, std::uint64_t divisor, std::uint64_t& quotient,
                      std::uint64_t& remainder) noexcept {
    if (divisor == 0 || numerator.hi >= divisor) {
        return false;
    }

    std::uint64_t rem = numerator.hi;
    std::uint64_t quot = 0;
    for (int bit = 63; bit >= 0; --bit) {
        // The bit shifted off the top of `rem` still counts: it represents 2^64,
        // which is necessarily >= divisor, so the subtraction is owed either way.
        const std::uint64_t carry = rem >> 63;
        rem = (rem << 1) | ((numerator.lo >> bit) & 1ull);
        quot <<= 1;
        if (carry != 0 || rem >= divisor) {
            rem -= divisor;
            quot |= 1ull;
        }
    }

    quotient = quot;
    remainder = rem;
    return true;
}

constexpr int compare(U128 lhs, U128 rhs) noexcept {
    if (lhs.hi != rhs.hi) {
        return lhs.hi < rhs.hi ? -1 : 1;
    }
    if (lhs.lo != rhs.lo) {
        return lhs.lo < rhs.lo ? -1 : 1;
    }
    return 0;
}

constexpr std::uint64_t greatest_common_divisor(std::uint64_t a, std::uint64_t b) noexcept {
    while (b != 0) {
        const std::uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/// Rebuilds a signed value from magnitude and sign, failing rather than
/// wrapping when the magnitude does not fit.
Result<std::int64_t> to_signed(std::uint64_t value, bool negative) {
    if (negative) {
        if (value > kInt64MinMagnitude) {
            return Error{Errc::invalid_argument, "signed overflow: magnitude exceeds INT64_MIN"};
        }
        if (value == kInt64MinMagnitude) {
            return std::numeric_limits<std::int64_t>::min();
        }
        return -static_cast<std::int64_t>(value);
    }
    if (value > kInt64MaxMagnitude) {
        return Error{Errc::invalid_argument, "signed overflow: magnitude exceeds INT64_MAX"};
    }
    return static_cast<std::int64_t>(value);
}

Result<std::int64_t> checked_multiply(std::int64_t a, std::int64_t b) {
    const U128 product = multiply(magnitude(a), magnitude(b));
    if (product.hi != 0) {
        return Error{Errc::invalid_argument, "integer overflow in multiplication"};
    }
    const bool negative = (a < 0) != (b < 0);
    return to_signed(product.lo, negative);
}

struct Normalised {
    std::int64_t num;
    std::int64_t den;
};

Result<Normalised> normalise(std::int64_t numerator, std::int64_t denominator) {
    if (denominator == 0) {
        return Error{Errc::invalid_argument, "rational denominator is zero"};
    }

    const bool negative = (numerator < 0) != (denominator < 0);
    std::uint64_t num = magnitude(numerator);
    std::uint64_t den = magnitude(denominator);

    if (num == 0) {
        return Normalised{0, 1};  // 0/n is 0/1, and never negative zero.
    }

    const std::uint64_t divisor = greatest_common_divisor(num, den);
    num /= divisor;
    den /= divisor;

    if (den > kInt64MaxMagnitude) {
        return Error{Errc::invalid_argument, "rational denominator is not representable"};
    }

    Result<std::int64_t> signed_num = to_signed(num, negative);
    if (!signed_num) {
        return signed_num.error().with_context("rational numerator");
    }
    return Normalised{signed_num.value(), static_cast<std::int64_t>(den)};
}

/// Applies `rounding` to a magnitude/remainder pair produced by an exact
/// division, given the sign of the true result.
Result<std::int64_t> apply_rounding(std::uint64_t quotient, std::uint64_t remainder,
                                    std::uint64_t divisor, bool negative, Rounding rounding) {
    bool increment = false;
    switch (rounding) {
        case Rounding::toward_zero:
            break;
        case Rounding::down:
            increment = negative && remainder != 0;
            break;
        case Rounding::up:
            increment = !negative && remainder != 0;
            break;
        case Rounding::nearest:
            // 2 * remainder >= divisor, written so the doubling cannot overflow.
            increment = remainder >= divisor - remainder;
            break;
    }

    if (increment) {
        if (quotient == std::numeric_limits<std::uint64_t>::max()) {
            return Error{Errc::invalid_argument, "integer overflow while rounding"};
        }
        ++quotient;
    }
    return to_signed(quotient, negative);
}

}  // namespace

Rational::Rational(std::int64_t numerator, std::int64_t denominator) {
    Result<Normalised> normalised = normalise(numerator, denominator);
    RF_CHECK_MSG(normalised.has_value(),
                 "Rational constructed from an invalid pair; use Rational::from() for "
                 "values that come from a file or a user");
    num_ = normalised.value().num;
    den_ = normalised.value().den;
}

Result<Rational> Rational::from(std::int64_t numerator, std::int64_t denominator) {
    Result<Normalised> normalised = normalise(numerator, denominator);
    if (!normalised) {
        return normalised.error();
    }
    Rational out;
    out.num_ = normalised.value().num;
    out.den_ = normalised.value().den;
    return out;
}

Result<Rational> Rational::inverse() const {
    if (num_ == 0) {
        return Error{Errc::invalid_argument, "cannot invert a zero rational"};
    }
    return Rational::from(den_, num_);
}

Result<Rational> Rational::multiplied_by(const Rational& other) const {
    // Cross-reduce before multiplying: (a/b)*(c/d) overflows far less often when
    // gcd(a,d) and gcd(c,b) are divided out first.
    const std::uint64_t g1 = greatest_common_divisor(magnitude(num_), magnitude(other.den_));
    const std::uint64_t g2 = greatest_common_divisor(magnitude(other.num_), magnitude(den_));

    const bool negative = (num_ < 0) != (other.num_ < 0);
    const U128 num = multiply(magnitude(num_) / g1, magnitude(other.num_) / g2);
    const U128 den = multiply(magnitude(den_) / g2, magnitude(other.den_) / g1);

    if (num.hi != 0 || den.hi != 0) {
        return Error{Errc::invalid_argument, "overflow multiplying rationals"};
    }

    Result<std::int64_t> signed_num = to_signed(num.lo, negative);
    if (!signed_num) {
        return signed_num.error().with_context("rational product");
    }
    if (den.lo > kInt64MaxMagnitude) {
        return Error{Errc::invalid_argument, "overflow multiplying rationals"};
    }
    return Rational::from(signed_num.value(), static_cast<std::int64_t>(den.lo));
}

Result<Rational> Rational::divided_by(const Rational& other) const {
    if (other.num_ == 0) {
        return Error{Errc::invalid_argument, "division by a zero rational"};
    }
    Result<Rational> inverted = other.inverse();
    if (!inverted) {
        return inverted.error();
    }
    return multiplied_by(inverted.value());
}

Result<Rational> Rational::plus(const Rational& other) const {
    const std::uint64_t g = greatest_common_divisor(magnitude(den_), magnitude(other.den_));
    const std::int64_t other_scale = other.den_ / static_cast<std::int64_t>(g);
    const std::int64_t self_scale = den_ / static_cast<std::int64_t>(g);

    Result<std::int64_t> lhs = checked_multiply(num_, other_scale);
    if (!lhs) {
        return lhs.error().with_context("rational addition");
    }
    Result<std::int64_t> rhs = checked_multiply(other.num_, self_scale);
    if (!rhs) {
        return rhs.error().with_context("rational addition");
    }
    Result<std::int64_t> den = checked_multiply(den_, other_scale);
    if (!den) {
        return den.error().with_context("rational addition");
    }

    const std::int64_t a = lhs.value();
    const std::int64_t b = rhs.value();
    if ((b > 0 && a > kInt64Max - b) ||
        (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b)) {
        return Error{Errc::invalid_argument, "overflow adding rationals"};
    }
    return Rational::from(a + b, den.value());
}

Result<Rational> Rational::minus(const Rational& other) const {
    if (other.num_ == std::numeric_limits<std::int64_t>::min()) {
        return Error{Errc::invalid_argument, "overflow negating rational"};
    }
    Result<Rational> negated = Rational::from(-other.num_, other.den_);
    if (!negated) {
        return negated.error();
    }
    return plus(negated.value());
}

double Rational::approximate() const noexcept {
    return static_cast<double>(num_) / static_cast<double>(den_);
}

std::string Rational::to_string() const {
    return std::to_string(num_) + "/" + std::to_string(den_);
}

bool operator<(const Rational& lhs, const Rational& rhs) noexcept {
    const bool lhs_negative = lhs.num_ < 0;
    const bool rhs_negative = rhs.num_ < 0;
    if (lhs_negative != rhs_negative) {
        return lhs_negative;
    }

    // Denominators are always positive, so cross-multiplying magnitudes at
    // 128-bit width orders the pair exactly, with no overflow and no doubles.
    const U128 left = multiply(magnitude(lhs.num_), magnitude(rhs.den_));
    const U128 right = multiply(magnitude(rhs.num_), magnitude(lhs.den_));
    const int ordering = compare(left, right);
    return lhs_negative ? ordering > 0 : ordering < 0;
}

Result<std::int64_t> mul_div(std::int64_t value, std::int64_t multiplier, std::int64_t divisor,
                             Rounding rounding) {
    if (divisor == 0) {
        return Error{Errc::invalid_argument, "mul_div by zero"};
    }

    const bool negative = ((value < 0) != (multiplier < 0)) != (divisor < 0);
    const U128 product = multiply(magnitude(value), magnitude(multiplier));
    const std::uint64_t den = magnitude(divisor);

    std::uint64_t quotient = 0;
    std::uint64_t remainder = 0;
    if (!divide(product, den, quotient, remainder)) {
        return Error{Errc::invalid_argument, "overflow in mul_div: result exceeds 64 bits"};
    }
    return apply_rounding(quotient, remainder, den, negative, rounding);
}

Result<std::int64_t> rescale(std::int64_t value, const Rational& from, const Rational& to,
                             Rounding rounding) {
    if (to.numerator() == 0) {
        return Error{Errc::invalid_argument, "cannot rescale into a zero time base"};
    }
    if (from.is_zero()) {
        return std::int64_t{0};
    }

    // value * (from.num/from.den) == out * (to.num/to.den)
    //   =>  out = value * from.num * to.den / (from.den * to.num)
    // Cross-reduce first so the intermediate products stay small.
    const std::uint64_t g_num =
        greatest_common_divisor(magnitude(from.numerator()), magnitude(to.numerator()));
    const std::uint64_t g_den =
        greatest_common_divisor(magnitude(from.denominator()), magnitude(to.denominator()));

    Result<std::int64_t> multiplier =
        checked_multiply(from.numerator() / static_cast<std::int64_t>(g_num),
                         to.denominator() / static_cast<std::int64_t>(g_den));
    if (!multiplier) {
        return multiplier.error().with_context("rescale");
    }
    Result<std::int64_t> divisor =
        checked_multiply(from.denominator() / static_cast<std::int64_t>(g_den),
                         to.numerator() / static_cast<std::int64_t>(g_num));
    if (!divisor) {
        return divisor.error().with_context("rescale");
    }

    return mul_div(value, multiplier.value(), divisor.value(), rounding);
}

}  // namespace rf::media
