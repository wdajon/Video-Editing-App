// Shared AVStream -> StreamInfo translation.
//
// Internal to rf_media: includes libav. Lives here rather than inside probe.cpp
// because the decoder must describe its stream the same way the probe does --
// two translations that drift apart would mean a file reports one frame rate
// when inspected and another when played.

#ifndef RF_MEDIA_STREAM_DESCRIPTION_HPP
#define RF_MEDIA_STREAM_DESCRIPTION_HPP

#include <optional>

#include "rf/media/media_info.hpp"

struct AVRational;
struct AVStream;

namespace rf::media::detail {

/// Converts an AVRational, dropping values the container did not really state.
///
/// Audio streams genuinely report 0/0 for frame rate, and some containers leave
/// aspect ratio at 0/1. Those are "not stated", not errors, and must never
/// become a plausible default that later gets used as though the file said it.
[[nodiscard]] std::optional<Rational> to_rational(const AVRational& value);

[[nodiscard]] StreamInfo describe_stream(const AVStream& stream);

}  // namespace rf::media::detail

#endif  // RF_MEDIA_STREAM_DESCRIPTION_HPP
