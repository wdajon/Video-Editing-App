// Exact rational arithmetic for media timelines.
//
// Every timestamp in ReelForge is an integer in a rational time base -- never a
// float, and never a duration in seconds. 1/30 and 1001/30000 are not
// representable in binary floating point, so a float pipeline accumulates error
// and lands a cut one frame off after a few thousand frames. That is the ±1
// drift the frame-accuracy rubric forbids, and it is unfixable downstream once
// the representation allows it.
//
// Rational is a value type: cheap to copy, always normalised (gcd-reduced,
// positive denominator), and comparable exactly.

#ifndef RF_MEDIA_RATIONAL_HPP
#define RF_MEDIA_RATIONAL_HPP

#include <cstdint>
#include <string>

#include "rf/core/result.hpp"

namespace rf::media {

/// How a conversion that does not land on an exact integer is resolved.
enum class Rounding {
    down,        ///< Toward negative infinity (floor). The default for "which frame contains this time".
    up,          ///< Toward positive infinity (ceil).
    toward_zero, ///< Truncate.
    nearest,     ///< Nearest; exact halves go away from zero.
};

/// A normalised rational number. Denominator is always > 0; numerator carries
/// the sign; the pair is always reduced by its greatest common divisor, so
/// equality is structural and `1001/30000 == 1001/30000` regardless of how each
/// side was constructed.
class Rational {
public:
    /// Zero (0/1).
    constexpr Rational() noexcept = default;

    /// Constructs from a known-good pair. A zero denominator is a programmer
    /// error here and trips RF_CHECK; use `from()` for values read out of a
    /// media file, where 0/0 is a thing that genuinely occurs.
    Rational(std::int64_t numerator, std::int64_t denominator);

    /// Constructs from untrusted input -- container metadata, project files,
    /// user entry. Returns an error rather than aborting.
    [[nodiscard]] static Result<Rational> from(std::int64_t numerator,
                                               std::int64_t denominator);

    [[nodiscard]] constexpr std::int64_t numerator() const noexcept { return num_; }
    [[nodiscard]] constexpr std::int64_t denominator() const noexcept { return den_; }

    [[nodiscard]] constexpr bool is_zero() const noexcept { return num_ == 0; }

    /// Multiplicative inverse. Fails for zero, and for the one value whose
    /// negation is not representable.
    [[nodiscard]] Result<Rational> inverse() const;

    /// Exact arithmetic. Each fails on overflow rather than wrapping silently:
    /// a wrapped timestamp is a seek to the wrong frame with no error reported.
    [[nodiscard]] Result<Rational> multiplied_by(const Rational& other) const;
    [[nodiscard]] Result<Rational> divided_by(const Rational& other) const;
    [[nodiscard]] Result<Rational> plus(const Rational& other) const;
    [[nodiscard]] Result<Rational> minus(const Rational& other) const;

    /// Approximate value, for display and for logging only. Deliberately not
    /// named `to_double`, and deliberately not an implicit conversion, so that
    /// using it inside timing logic is a visible choice at the call site.
    [[nodiscard]] double approximate() const noexcept;

    /// "1001/30000"
    [[nodiscard]] std::string to_string() const;

    friend bool operator==(const Rational& lhs, const Rational& rhs) noexcept {
        return lhs.num_ == rhs.num_ && lhs.den_ == rhs.den_;
    }

    /// Exact ordering, computed without overflow and without floating point.
    friend bool operator<(const Rational& lhs, const Rational& rhs) noexcept;
    friend bool operator>(const Rational& lhs, const Rational& rhs) noexcept { return rhs < lhs; }
    friend bool operator<=(const Rational& lhs, const Rational& rhs) noexcept { return !(rhs < lhs); }
    friend bool operator>=(const Rational& lhs, const Rational& rhs) noexcept { return !(lhs < rhs); }

private:
    std::int64_t num_ = 0;
    std::int64_t den_ = 1;
};

/// Converts a timestamp from one time base to another, exactly.
///
///   rescale(90000, {1, 90000}, {1, 1000})  ->  1000   (1 second, ms base)
///
/// The intermediate product is computed at 128-bit width, so a 4K/10-minute
/// timeline in a 1/90000 base cannot overflow into a wrong-but-plausible
/// timestamp. A result that genuinely does not fit in int64 is an error, never
/// a wrapped value.
[[nodiscard]] Result<std::int64_t> rescale(std::int64_t value,
                                           const Rational& from,
                                           const Rational& to,
                                           Rounding rounding = Rounding::nearest);

/// Computes (value * multiplier) / divisor with a 128-bit intermediate.
/// Exposed because timestamp code needs it directly, and because a hand-rolled
/// `a * b / c` in int64 is the single most common source of timestamp overflow.
[[nodiscard]] Result<std::int64_t> mul_div(std::int64_t value,
                                           std::int64_t multiplier,
                                           std::int64_t divisor,
                                           Rounding rounding = Rounding::nearest);

}  // namespace rf::media

#endif  // RF_MEDIA_RATIONAL_HPP
