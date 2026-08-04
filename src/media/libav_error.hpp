// Translation from libav's negative int error codes into rf::Error.
//
// Internal to rf_media: this header includes libav, so nothing outside
// src/media/ may include it.

#ifndef RF_MEDIA_LIBAV_ERROR_HPP
#define RF_MEDIA_LIBAV_ERROR_HPP

#include <source_location>
#include <string_view>

#include "rf/core/error.hpp"

namespace rf::media::detail {

/// Builds an Error from a libav return code.
///
/// The resulting message keeps both libav's own text (via av_strerror) and the
/// numeric code, because the text alone is frequently "Invalid argument" and
/// the code is what makes a bug report actionable.
[[nodiscard]] Error from_libav(int averror,
                               std::string_view context,
                               std::source_location origin = std::source_location::current());

/// Raises libav's log threshold to errors only, once per process.
///
/// libav writes warnings straight to stderr by default, which turns a test run
/// over deliberately damaged fixtures into pages of noise. This is global,
/// process-wide state owned by a third-party library; routing libav logging
/// into ReelForge's own logger is the real fix and is tracked in
/// docs/BACKLOG.md.
void quiet_libav_logging_once() noexcept;

}  // namespace rf::media::detail

#endif  // RF_MEDIA_LIBAV_ERROR_HPP
