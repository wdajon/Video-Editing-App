#include "rf/media/convert.hpp"

#include <memory>
#include <string>
#include <vector>

#include "libav_error.hpp"

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

namespace rf::media {
namespace {

struct SwsContextDeleter {
    void operator()(SwsContext* context) const noexcept {
        if (context != nullptr) {
            sws_freeContext(context);
        }
    }
};

using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;

}  // namespace

Result<VideoFrame> to_rgba8(const VideoFrame& frame) {
    if (frame.is_empty() || frame.width() <= 0 || frame.height() <= 0) {
        return Error{Errc::invalid_argument, "cannot convert an empty frame"};
    }
    if (frame.pixel_format() == "rgba") {
        return frame;
    }

    // The frame carries its format as a name rather than a libav enum, so that
    // nothing above rf_media sees a libav type (ADR 004). Mapping it back here
    // is the cost of that, and it is cheap.
    const AVPixelFormat source_format = av_get_pix_fmt(frame.pixel_format().c_str());
    if (source_format == AV_PIX_FMT_NONE) {
        return Error{Errc::unsupported_format,
                     "unrecognised pixel format: " + frame.pixel_format()};
    }

    SwsContextPtr context{sws_getContext(frame.width(), frame.height(), source_format,
                                         frame.width(), frame.height(), AV_PIX_FMT_RGBA,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr)};
    if (!context) {
        return Error{Errc::unsupported_format,
                     "swscale cannot convert " + frame.pixel_format() + " to rgba"};
    }

    // The source is tightly packed, so its plane pointers and strides have to be
    // derived rather than taken from an AVFrame.
    const std::uint8_t* source_planes[4] = {};
    int source_strides[4] = {};
    const int filled = av_image_fill_arrays(
        const_cast<std::uint8_t**>(source_planes), source_strides, frame.pixels().data(),
        source_format, frame.width(), frame.height(), 1);
    if (filled < 0) {
        return detail::from_libav(filled, "cannot describe the source frame");
    }
    if (static_cast<std::size_t>(filled) != frame.pixels().size()) {
        return Error{Errc::corrupt_data,
                     "frame buffer is " + std::to_string(frame.pixels().size()) +
                         " bytes, but " + frame.pixel_format() + " at " +
                         std::to_string(frame.width()) + "x" + std::to_string(frame.height()) +
                         " needs " + std::to_string(filled)};
    }

    std::vector<std::uint8_t> destination(static_cast<std::size_t>(frame.width()) *
                                          static_cast<std::size_t>(frame.height()) * 4u);
    std::uint8_t* destination_planes[4] = {destination.data(), nullptr, nullptr, nullptr};
    int destination_strides[4] = {frame.width() * 4, 0, 0, 0};

    const int rows = sws_scale(context.get(), source_planes, source_strides, 0, frame.height(),
                               destination_planes, destination_strides);
    if (rows != frame.height()) {
        return Error{Errc::internal, "swscale converted " + std::to_string(rows) + " of " +
                                         std::to_string(frame.height()) + " rows"};
    }

    VideoFrame out{frame.width(), frame.height(), "rgba", std::move(destination)};
    out.presentation_timestamp = frame.presentation_timestamp;
    out.time_base = frame.time_base;
    out.frame_index = frame.frame_index;
    return out;
}

}  // namespace rf::media
