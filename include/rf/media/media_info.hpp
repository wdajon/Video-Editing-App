// Container and stream description, as reported by a probe.
//
// This header is deliberately free of libav types. Everything above rf_media --
// the timeline model, the render graph, the UI -- reads media through these
// structs, so that no other layer ends up including a libav header. See
// docs/adr/004-ffmpeg-linkage.md.
//
// Fields that a container may legitimately not know are std::optional rather
// than a sentinel value. A duration of -9223372036854775808 silently used as a
// number is a class of bug this type refuses to enable.

#ifndef RF_MEDIA_MEDIA_INFO_HPP
#define RF_MEDIA_MEDIA_INFO_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "rf/media/rational.hpp"

namespace rf::media {

enum class StreamKind {
    video,
    audio,
    subtitle,
    data,
    attachment,
    unknown,
};

[[nodiscard]] std::string_view to_string(StreamKind kind) noexcept;

struct VideoStreamInfo {
    int width = 0;
    int height = 0;

    /// Average frame rate. Absent when the container does not state one, which
    /// is normal for variable-frame-rate material and for some raw streams.
    /// Never silently defaulted to 25 or 30 -- guessing here is how an export
    /// ends up a different length than the source.
    std::optional<Rational> average_frame_rate;

    /// Rate derived from the smallest frame duration. For CFR material this
    /// equals average_frame_rate; where they differ the source is VFR.
    std::optional<Rational> real_frame_rate;

    /// Pixel aspect ratio. Absent means unspecified, which callers should treat
    /// as 1:1 -- but the distinction between "stated as square" and "not
    /// stated" is preserved here rather than collapsed.
    std::optional<Rational> sample_aspect_ratio;

    std::string pixel_format;
};

struct AudioStreamInfo {
    int sample_rate = 0;
    int channel_count = 0;
    std::string sample_format;
};

struct StreamInfo {
    int index = 0;
    StreamKind kind = StreamKind::unknown;

    /// Codec short name as libav reports it ("h264", "aac"). Empty when the
    /// codec is not recognised, which is not an error: an unknown subtitle
    /// stream must not prevent the file from opening.
    std::string codec_name;

    /// The unit every timestamp on this stream is expressed in.
    Rational time_base;

    /// Duration and start time, in `time_base` units. Absent when unknown.
    std::optional<std::int64_t> duration;
    std::optional<std::int64_t> start_time;

    /// Frame count as *claimed by the container*. Absent when not stated, and
    /// not to be trusted for seeking: containers lie, and the honest count
    /// comes from decoding. Present here because it is useful for diagnostics
    /// and for cross-checking.
    std::optional<std::int64_t> container_frame_count;

    std::optional<VideoStreamInfo> video;
    std::optional<AudioStreamInfo> audio;
};

struct MediaInfo {
    /// Comma-separated demuxer names, e.g. "mov,mp4,m4a,3gp,3g2,mj2".
    std::string format_name;

    /// Whole-container duration in microseconds. Absent when unknown.
    std::optional<std::int64_t> duration_microseconds;

    /// Overall bit rate in bits per second. Absent when unknown.
    std::optional<std::int64_t> bit_rate;

    std::vector<StreamInfo> streams;

    /// First video / audio stream, or nullptr. Returns a pointer rather than a
    /// reference because "this file has no video" is an ordinary case that
    /// callers must handle, not an error.
    [[nodiscard]] const StreamInfo* primary_video() const noexcept;
    [[nodiscard]] const StreamInfo* primary_audio() const noexcept;
};

}  // namespace rf::media

#endif  // RF_MEDIA_MEDIA_INFO_HPP
