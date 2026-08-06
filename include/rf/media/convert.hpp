// Pixel format conversion.
//
// Decoders produce planar YUV; the compositor consumes packed RGBA. Something
// has to bridge them.
//
// This does it on the CPU with swscale, which is correct, portable and the
// obvious first implementation. It is also, at 1080x1920, a per-frame cost on
// the render path -- the same class of cost that made compositing miss its
// budget by 200x before the layers moved onto the device (D13). Converting in a
// shader, by uploading the Y, U and V planes and doing the matrix on the GPU,
// is the eventual answer; this exists so the cost can be measured rather than
// assumed, and so export has a conversion at all.

#ifndef RF_MEDIA_CONVERT_HPP
#define RF_MEDIA_CONVERT_HPP

#include "rf/core/result.hpp"
#include "rf/media/video_frame.hpp"

namespace rf::media {

/// Converts a decoded frame to packed 8-bit RGBA.
///
/// The result carries pixel_format "rgba" and the same geometry, timestamp and
/// frame index as the input. A frame already in that format is returned
/// unchanged rather than round-tripped through swscale.
[[nodiscard]] Result<VideoFrame> to_rgba8(const VideoFrame& frame);

}  // namespace rf::media

#endif  // RF_MEDIA_CONVERT_HPP
