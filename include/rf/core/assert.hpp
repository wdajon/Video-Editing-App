// Hard invariant checks.
//
// RF_CHECK is active in every build configuration, release included. A violated
// invariant in a video editor means the timeline model and the render graph
// disagree; continuing from that point corrupts the user's project on the next
// autosave. Failing loudly and immediately is the lesser harm.
//
// Use RF_CHECK for programmer errors only. Anything a user or a file can cause
// -- malformed media, missing path, disk full -- is a Result, never a check.

#ifndef RF_CORE_ASSERT_HPP
#define RF_CORE_ASSERT_HPP

#include <source_location>
#include <string_view>

namespace rf::detail {

/// Writes the diagnostic to stderr and aborts. Never returns.
[[noreturn]] void fail_check(std::string_view expression,
                             std::string_view message,
                             const std::source_location& origin) noexcept;

}  // namespace rf::detail

#define RF_CHECK(expr)                                                        \
    (static_cast<bool>(expr)                                                  \
         ? static_cast<void>(0)                                               \
         : ::rf::detail::fail_check(#expr, "", ::std::source_location::current()))

#define RF_CHECK_MSG(expr, msg)                                               \
    (static_cast<bool>(expr)                                                  \
         ? static_cast<void>(0)                                               \
         : ::rf::detail::fail_check(#expr, (msg), ::std::source_location::current()))

#endif  // RF_CORE_ASSERT_HPP
