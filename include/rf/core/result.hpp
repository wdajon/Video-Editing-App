// rf::Result<T, E> -- the return type of every fallible ReelForge operation.
//
// This is a std::expected-shaped type implemented for C++20 (std::expected is
// C++23 and not uniformly available across the compilers in the support matrix
// -- see docs/adr/002-error-handling.md). It never throws, and it is marked
// [[nodiscard]] so an ignored failure is a compile error, not a silent bug.
//
//   Result<Frame> decode_frame(int64_t pts);
//
//   auto frame = decode_frame(pts);
//   if (!frame) { return frame.error().with_context("scrub"); }
//   use(frame.value());
//
// Result<void> is supported and means "succeeded or failed, no payload".

#ifndef RF_CORE_RESULT_HPP
#define RF_CORE_RESULT_HPP

#include <functional>
#include <type_traits>
#include <utility>
#include <variant>

#include "rf/core/assert.hpp"
#include "rf/core/error.hpp"

namespace rf {

template <class T, class E = Error>
class Result;

namespace detail {

/// Stand-in payload for Result<void>. Empty, trivial, always equal to itself.
struct VoidValue {
    friend constexpr bool operator==(VoidValue, VoidValue) noexcept { return true; }
};

template <class>
struct is_result : std::false_type {};
template <class T, class E>
struct is_result<Result<T, E>> : std::true_type {};
template <class T>
inline constexpr bool is_result_v = is_result<std::remove_cvref_t<T>>::value;

// Lazily selects the right invoke_result: a Result<void> continuation takes no
// argument, every other continuation takes the value. Written as a partial
// specialization so only the selected branch is ever instantiated.
template <bool IsVoid, class F, class Arg>
struct value_invoke_result {
    using type = std::invoke_result_t<F, Arg>;
};
template <class F, class Arg>
struct value_invoke_result<true, F, Arg> {
    using type = std::invoke_result_t<F>;
};
template <bool IsVoid, class F, class Arg>
using value_invoke_result_t = typename value_invoke_result<IsVoid, F, Arg>::type;

template <bool IsVoid, class F, class Arg>
constexpr decltype(auto) invoke_value(F&& f, Arg&& arg) {
    if constexpr (IsVoid) {
        static_cast<void>(arg);
        return std::invoke(std::forward<F>(f));
    } else {
        return std::invoke(std::forward<F>(f), std::forward<Arg>(arg));
    }
}

}  // namespace detail

template <class T, class E>
class [[nodiscard]] Result {
    static_assert(!std::is_reference_v<T>,
                  "Result<T&> is not supported; use a pointer or std::reference_wrapper");
    static_assert(!std::is_reference_v<E>, "Result<T, E&> is not supported");
    static_assert(!std::is_void_v<E>, "Result error type cannot be void");

    static constexpr bool kVoidValue = std::is_void_v<T>;
    using stored_type = std::conditional_t<kVoidValue, detail::VoidValue, T>;

    static_assert(!std::is_same_v<std::remove_cv_t<stored_type>, std::remove_cv_t<E>>,
                  "Result<T, E> requires distinct value and error types");

public:
    using value_type = T;
    using error_type = E;

    /// Success. Available for Result<void> and for default-constructible values.
    constexpr Result()
        requires(std::is_default_constructible_v<stored_type>)
        : storage_(std::in_place_index<0>) {}

    /// Success carrying a value. Implicit so `return frame;` works.
    template <class U = stored_type>
        requires(!kVoidValue && std::is_constructible_v<stored_type, U &&> &&
                 !std::is_same_v<std::remove_cvref_t<U>, Result> &&
                 !std::is_same_v<std::remove_cvref_t<U>, E>)
    // NOLINTNEXTLINE(google-explicit-constructor)
    constexpr Result(U&& value) : storage_(std::in_place_index<0>, std::forward<U>(value)) {}

    /// Failure. Implicit so `return Error{Errc::not_found, "..."};` works.
    // NOLINTNEXTLINE(google-explicit-constructor)
    constexpr Result(E error) : storage_(std::in_place_index<1>, std::move(error)) {}

    [[nodiscard]] constexpr bool has_value() const noexcept { return storage_.index() == 0; }
    [[nodiscard]] constexpr bool has_error() const noexcept { return storage_.index() == 1; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_value(); }

    // --- value access -------------------------------------------------------
    // Reading the value of a failed Result is a programmer error, not a runtime
    // condition, so it trips RF_CHECK rather than returning a default. Returning
    // a default frame here is precisely how a decode failure becomes a silently
    // black export.

    [[nodiscard]] constexpr stored_type& value() & requires(!kVoidValue) {
        RF_CHECK_MSG(has_value(), "Result::value() called on a failed Result");
        return *std::get_if<0>(&storage_);
    }
    [[nodiscard]] constexpr const stored_type& value() const& requires(!kVoidValue) {
        RF_CHECK_MSG(has_value(), "Result::value() called on a failed Result");
        return *std::get_if<0>(&storage_);
    }
    [[nodiscard]] constexpr stored_type&& value() && requires(!kVoidValue) {
        RF_CHECK_MSG(has_value(), "Result::value() called on a failed Result");
        return std::move(*std::get_if<0>(&storage_));
    }

    [[nodiscard]] constexpr stored_type* operator->() requires(!kVoidValue) {
        RF_CHECK_MSG(has_value(), "Result::operator-> called on a failed Result");
        return std::get_if<0>(&storage_);
    }
    [[nodiscard]] constexpr const stored_type* operator->() const requires(!kVoidValue) {
        RF_CHECK_MSG(has_value(), "Result::operator-> called on a failed Result");
        return std::get_if<0>(&storage_);
    }

    [[nodiscard]] constexpr stored_type& operator*() & requires(!kVoidValue) { return value(); }
    [[nodiscard]] constexpr const stored_type& operator*() const& requires(!kVoidValue) { return value(); }
    [[nodiscard]] constexpr stored_type&& operator*() && requires(!kVoidValue) {
        return std::move(*this).value();
    }

    // --- error access -------------------------------------------------------

    [[nodiscard]] constexpr E& error() & {
        RF_CHECK_MSG(has_error(), "Result::error() called on a successful Result");
        return *std::get_if<1>(&storage_);
    }
    [[nodiscard]] constexpr const E& error() const& {
        RF_CHECK_MSG(has_error(), "Result::error() called on a successful Result");
        return *std::get_if<1>(&storage_);
    }
    [[nodiscard]] constexpr E&& error() && {
        RF_CHECK_MSG(has_error(), "Result::error() called on a successful Result");
        return std::move(*std::get_if<1>(&storage_));
    }

    /// The value, or `fallback` if this Result holds an error. Never checks.
    template <class U>
    [[nodiscard]] constexpr stored_type value_or(U&& fallback) const& requires(!kVoidValue) {
        return has_value() ? *std::get_if<0>(&storage_)
                           : static_cast<stored_type>(std::forward<U>(fallback));
    }
    template <class U>
    [[nodiscard]] constexpr stored_type value_or(U&& fallback) && requires(!kVoidValue) {
        return has_value() ? std::move(*std::get_if<0>(&storage_))
                           : static_cast<stored_type>(std::forward<U>(fallback));
    }

    // --- composition --------------------------------------------------------

    /// Chain a fallible step. `f` must return a Result with the same error type.
    template <class F>
    [[nodiscard]] constexpr auto and_then(F&& f) && {
        using R = detail::value_invoke_result_t<kVoidValue, F, stored_type&&>;
        static_assert(detail::is_result_v<R>, "and_then callable must return an rf::Result");
        static_assert(std::is_same_v<typename std::remove_cvref_t<R>::error_type, E>,
                      "and_then callable must preserve the error type; use map_error first");
        if (has_error()) {
            return std::remove_cvref_t<R>(std::move(*std::get_if<1>(&storage_)));
        }
        return std::remove_cvref_t<R>(
            detail::invoke_value<kVoidValue>(std::forward<F>(f), std::move(*std::get_if<0>(&storage_))));
    }

    template <class F>
    [[nodiscard]] constexpr auto and_then(F&& f) const& {
        using R = detail::value_invoke_result_t<kVoidValue, F, const stored_type&>;
        static_assert(detail::is_result_v<R>, "and_then callable must return an rf::Result");
        static_assert(std::is_same_v<typename std::remove_cvref_t<R>::error_type, E>,
                      "and_then callable must preserve the error type; use map_error first");
        if (has_error()) {
            return std::remove_cvref_t<R>(*std::get_if<1>(&storage_));
        }
        return std::remove_cvref_t<R>(
            detail::invoke_value<kVoidValue>(std::forward<F>(f), *std::get_if<0>(&storage_)));
    }

    /// Transform the value, leaving an error untouched. `f` returns a plain
    /// value (or void, producing Result<void, E>).
    template <class F>
    [[nodiscard]] constexpr auto map(F&& f) && {
        using U = detail::value_invoke_result_t<kVoidValue, F, stored_type&&>;
        static_assert(!detail::is_result_v<U>, "map callable returns a Result; use and_then");
        if constexpr (std::is_void_v<U>) {
            if (has_error()) {
                return Result<void, E>(std::move(*std::get_if<1>(&storage_)));
            }
            detail::invoke_value<kVoidValue>(std::forward<F>(f), std::move(*std::get_if<0>(&storage_)));
            return Result<void, E>();
        } else {
            if (has_error()) {
                return Result<U, E>(std::move(*std::get_if<1>(&storage_)));
            }
            return Result<U, E>(detail::invoke_value<kVoidValue>(
                std::forward<F>(f), std::move(*std::get_if<0>(&storage_))));
        }
    }

    template <class F>
    [[nodiscard]] constexpr auto map(F&& f) const& {
        using U = detail::value_invoke_result_t<kVoidValue, F, const stored_type&>;
        static_assert(!detail::is_result_v<U>, "map callable returns a Result; use and_then");
        if constexpr (std::is_void_v<U>) {
            if (has_error()) {
                return Result<void, E>(*std::get_if<1>(&storage_));
            }
            detail::invoke_value<kVoidValue>(std::forward<F>(f), *std::get_if<0>(&storage_));
            return Result<void, E>();
        } else {
            if (has_error()) {
                return Result<U, E>(*std::get_if<1>(&storage_));
            }
            return Result<U, E>(
                detail::invoke_value<kVoidValue>(std::forward<F>(f), *std::get_if<0>(&storage_)));
        }
    }

    /// Transform the error, leaving a value untouched.
    template <class F>
    [[nodiscard]] constexpr auto map_error(F&& f) && {
        using G = std::invoke_result_t<F, E&&>;
        static_assert(!std::is_void_v<G>, "map_error callable must return an error type");
        if (has_value()) {
            if constexpr (kVoidValue) {
                return Result<T, G>();
            } else {
                return Result<T, G>(std::move(*std::get_if<0>(&storage_)));
            }
        }
        return Result<T, G>(std::invoke(std::forward<F>(f), std::move(*std::get_if<1>(&storage_))));
    }

    template <class F>
    [[nodiscard]] constexpr auto map_error(F&& f) const& {
        using G = std::invoke_result_t<F, const E&>;
        static_assert(!std::is_void_v<G>, "map_error callable must return an error type");
        if (has_value()) {
            if constexpr (kVoidValue) {
                return Result<T, G>();
            } else {
                return Result<T, G>(*std::get_if<0>(&storage_));
            }
        }
        return Result<T, G>(std::invoke(std::forward<F>(f), *std::get_if<1>(&storage_)));
    }

    friend constexpr bool operator==(const Result& lhs, const Result& rhs) {
        if (lhs.storage_.index() != rhs.storage_.index()) {
            return false;
        }
        if (lhs.has_value()) {
            return *std::get_if<0>(&lhs.storage_) == *std::get_if<0>(&rhs.storage_);
        }
        return *std::get_if<1>(&lhs.storage_) == *std::get_if<1>(&rhs.storage_);
    }

private:
    std::variant<stored_type, E> storage_;
};

/// Convenience for the common `Result<void>` success return.
[[nodiscard]] inline constexpr Result<void> ok() noexcept {
    return Result<void>();
}

}  // namespace rf

#endif  // RF_CORE_RESULT_HPP
