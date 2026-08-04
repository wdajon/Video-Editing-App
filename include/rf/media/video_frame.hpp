// A decoded video frame, owned as plain bytes.
//
// Deliberately not an AVFrame wrapper: nothing above rf_media may see a libav
// type (ADR 004). The pixel data is tightly packed -- no stride padding -- so
// that a hash over it is identical on every platform and every decoder build.
// That property is what makes it usable as the oracle for seek accuracy.
//
// This copies out of the decoder. Correct and portable, but a copy per frame is
// not what the M3 playback budget will tolerate; zero-copy hand-off to the GPU
// is tracked in docs/BACKLOG.md and is an M3 concern, not an M1 one.

#ifndef RF_MEDIA_VIDEO_FRAME_HPP
#define RF_MEDIA_VIDEO_FRAME_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "rf/media/rational.hpp"

namespace rf::media {

class VideoFrame {
public:
    VideoFrame() = default;

    VideoFrame(int width, int height, std::string pixel_format, std::vector<std::uint8_t> pixels)
        : pixels_(std::move(pixels)),
          pixel_format_(std::move(pixel_format)),
          width_(width),
          height_(height) {}

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] const std::string& pixel_format() const noexcept { return pixel_format_; }

    /// Tightly packed pixel data, plane after plane, no row padding.
    [[nodiscard]] const std::vector<std::uint8_t>& pixels() const noexcept { return pixels_; }

    /// Presentation timestamp in `time_base` units. Absent when the container
    /// and decoder between them could not supply one -- which happens, and must
    /// not be papered over with a guess.
    std::optional<std::int64_t> presentation_timestamp;

    /// Unit the timestamp above is expressed in.
    Rational time_base;

    /// Index of this frame counting from the start of the stream, assigned by
    /// the decoder that produced it. Absent when the frame did not come from a
    /// sequential decode.
    std::optional<std::int64_t> frame_index;

    [[nodiscard]] bool is_empty() const noexcept { return pixels_.empty(); }

private:
    std::vector<std::uint8_t> pixels_;
    std::string pixel_format_;
    int width_ = 0;
    int height_ = 0;
};

/// Stable 64-bit content hash of a frame's pixels, geometry and format.
///
/// FNV-1a over the packed bytes: not cryptographic, and not meant to be. It
/// exists so that "the frame reached by seeking is the frame reached by
/// decoding linearly" is a single comparison. Because the input is tightly
/// packed, the value is reproducible across platforms and builds.
[[nodiscard]] std::uint64_t frame_hash(const VideoFrame& frame) noexcept;

}  // namespace rf::media

#endif  // RF_MEDIA_VIDEO_FRAME_HPP
