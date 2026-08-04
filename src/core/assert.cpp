#include "rf/core/assert.hpp"

#include <cstdio>
#include <cstdlib>

namespace rf::detail {

void fail_check(std::string_view expression,
                std::string_view message,
                const std::source_location& origin) noexcept {
    // Deliberately uses stdio rather than the logging subsystem: a failed check
    // may itself be a broken logger, and this path must not allocate.
    std::fprintf(stderr,
                 "ReelForge fatal: check failed: %.*s\n"
                 "  message: %.*s\n"
                 "  at: %s:%u in %s\n",
                 static_cast<int>(expression.size()), expression.data(),
                 static_cast<int>(message.size()), message.data(),
                 origin.file_name() != nullptr ? origin.file_name() : "<unknown>",
                 origin.line(),
                 origin.function_name() != nullptr ? origin.function_name() : "<unknown>");
    std::fflush(stderr);
    std::abort();
}

}  // namespace rf::detail
