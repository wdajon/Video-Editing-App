// ReelForge core error type.
//
// ReelForge does not throw across module boundaries. Fallible operations return
// rf::Result<T> (see result.hpp) carrying this Error on failure. An Error always
// records where it was constructed so a failure surfaced at the UI layer can be
// traced back to the libav / GPU / IO call that produced it.

#ifndef RF_CORE_ERROR_HPP
#define RF_CORE_ERROR_HPP

#include <cstdint>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

namespace rf {

/// Stable, exhaustive failure taxonomy. Codes are never renumbered: they are
/// written into crash reports and log files that outlive a given build.
enum class Errc : std::uint16_t {
    invalid_argument = 1,   ///< Caller passed a value outside the documented domain.
    not_found = 2,          ///< Named resource (file, track, node, preset) does not exist.
    io_failure = 3,         ///< Read/write/seek failed at the OS level.
    permission_denied = 4,  ///< Resource exists but access was refused.
    unsupported_format = 5, ///< Container/codec/pixel format we cannot handle.
    corrupt_data = 6,       ///< Bitstream or project document failed validation.
    decode_failure = 7,     ///< Decoder rejected otherwise well-formed input.
    encode_failure = 8,     ///< Encoder or muxer failed mid-write.
    out_of_memory = 9,      ///< Host or device allocation failed.
    device_lost = 10,       ///< GPU device removed or reset.
    cancelled = 11,         ///< Operation aborted at user or scheduler request.
    timeout = 12,           ///< Operation exceeded its deadline.
    version_mismatch = 13,  ///< Project/preset/plugin schema version not migratable.
    already_exists = 14,    ///< Creation would overwrite an existing resource.
    internal = 15,          ///< Invariant violated inside ReelForge; always a bug.
};

/// Short, stable, machine-greppable spelling of a code (e.g. "decode_failure").
/// Returns "unknown" for values outside the enumeration rather than asserting,
/// because logging must never be the thing that takes the process down.
[[nodiscard]] std::string_view to_string(Errc code) noexcept;

/// A failure: a code, a human-readable message, and the origin site.
///
/// Error is a value type. Copying it copies the message; move is cheap. It is
/// deliberately not derived from std::exception -- it is never thrown.
class Error {
public:
    Error(Errc code,
          std::string message,
          std::source_location origin = std::source_location::current())
        : message_(std::move(message)), origin_(origin), code_(code) {}

    // rf:: is required: the member to_string() below hides the free function
    // inside the class scope.
    Error(Errc code, std::source_location origin = std::source_location::current())
        : message_(rf::to_string(code)), origin_(origin), code_(code) {}

    [[nodiscard]] Errc code() const noexcept { return code_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] const std::source_location& origin() const noexcept { return origin_; }

    /// "decode_failure: no keyframe before pts 12345 (at src/media/decoder.cpp:214)"
    [[nodiscard]] std::string to_string() const;

    /// Prepend context while preserving the original code and origin, so a
    /// failure can gain call-site meaning without losing where it started.
    [[nodiscard]] Error with_context(std::string_view context) const;

    friend bool operator==(const Error& lhs, const Error& rhs) noexcept {
        return lhs.code_ == rhs.code_ && lhs.message_ == rhs.message_;
    }

private:
    std::string message_;
    std::source_location origin_;
    Errc code_;
};

}  // namespace rf

#endif  // RF_CORE_ERROR_HPP
