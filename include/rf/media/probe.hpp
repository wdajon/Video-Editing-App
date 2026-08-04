// Reading a media file's structure without decoding it.

#ifndef RF_MEDIA_PROBE_HPP
#define RF_MEDIA_PROBE_HPP

#include <filesystem>

#include "rf/core/result.hpp"
#include "rf/media/media_info.hpp"

namespace rf::media {

/// Opens `path`, reads container and stream metadata, and closes it again.
///
/// Decodes only as much as libav needs to describe the streams -- enough to
/// report a codec and a frame rate, not enough to be a substitute for opening
/// the file for playback.
///
/// Failure modes this reports rather than crashing on: the file does not exist,
/// is not readable, is not media at all, is media in a format ReelForge was not
/// built with, or is truncated. All of those are things a user does by accident
/// on an ordinary afternoon.
[[nodiscard]] Result<MediaInfo> probe_file(const std::filesystem::path& path);

}  // namespace rf::media

#endif  // RF_MEDIA_PROBE_HPP
