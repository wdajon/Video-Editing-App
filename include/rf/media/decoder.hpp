// Sequential video decoding.
//
// M1 scope: open a file, walk its video stream frame by frame. Seeking arrives
// in the next increment and is built on this, because linear decode is the
// oracle seek accuracy is measured against.

#ifndef RF_MEDIA_DECODER_HPP
#define RF_MEDIA_DECODER_HPP

#include <filesystem>
#include <memory>
#include <optional>

#include "rf/core/result.hpp"
#include "rf/media/media_info.hpp"
#include "rf/media/video_frame.hpp"

namespace rf::media {

/// Decodes the video stream of a media file, in order.
///
/// Move-only: it owns a demuxer and a decoder context, and copying those is
/// meaningless. All libav state is behind a pimpl so that no libav type appears
/// in this header (ADR 004).
class VideoDecoder {
public:
    /// Opens `path` and selects its best video stream. Fails if the file cannot
    /// be read, contains no video, or uses a codec this build lacks.
    [[nodiscard]] static Result<VideoDecoder> open(const std::filesystem::path& path);

    VideoDecoder(VideoDecoder&&) noexcept;
    VideoDecoder& operator=(VideoDecoder&&) noexcept;
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;
    ~VideoDecoder();

    /// Description of the stream being decoded.
    [[nodiscard]] const StreamInfo& stream() const noexcept;

    /// Decodes the next frame in presentation order.
    ///
    /// Returns an empty optional at end of stream -- which is an outcome, not a
    /// failure, and is why this is `Result<optional<Frame>>` rather than an
    /// error code that callers would have to special-case.
    [[nodiscard]] Result<std::optional<VideoFrame>> next_frame();

    /// Positions the decoder so the next `next_frame()` returns exactly frame
    /// `index`, counting from zero at the start of the stream.
    ///
    /// Frame-accurate, not keyframe-accurate: it seeks to the keyframe at or
    /// before the target and decodes forward to the exact frame, because
    /// landing on the nearest keyframe and calling it a seek is how an editor
    /// cuts in the wrong place.
    ///
    /// Requires the stream to state a frame rate. Variable-frame-rate material
    /// has no frame-index-to-time mapping, so this reports an error there
    /// rather than inventing one -- use `seek_to_timestamp` instead.
    [[nodiscard]] Result<void> seek_to_frame(std::int64_t index);

    /// Positions the decoder at the first frame whose presentation timestamp is
    /// at or after `timestamp`, expressed in `stream().time_base` units.
    ///
    /// "At or after", not "containing": a playhead in ReelForge sits on a frame
    /// boundary, and containment semantics belong with the timeline model.
    [[nodiscard]] Result<void> seek_to_timestamp(std::int64_t timestamp);

    /// Number of frames returned to the caller so far. The honest count, as
    /// opposed to whatever the container claimed. Not reset by seeking -- it
    /// counts work done, not position.
    [[nodiscard]] std::int64_t frames_decoded() const noexcept;

    /// Number of frames copied out of libav into an owned buffer.
    ///
    /// Exposed for tests, and it earns its place: a seek decodes many frames to
    /// reach its target but must materialise only the one it was asked for.
    /// Copying the discarded frames too is invisible to every correctness test
    /// and cost a 5.8x seek slowdown at 4K before it was found by measurement.
    /// Asserting on this counter catches that regression deterministically, at
    /// any resolution, without timing anything.
    [[nodiscard]] std::int64_t frames_materialised() const noexcept;

private:
    class Impl;
    explicit VideoDecoder(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rf::media

#endif  // RF_MEDIA_DECODER_HPP
