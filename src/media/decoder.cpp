#include "rf/media/decoder.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "libav_error.hpp"
#include "stream_description.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
}

namespace rf::media {
namespace {

// Each libav resource gets an RAII owner. There are no bare av_*_free calls in
// this file; that is the point of ADR 004's "no raw lifetime management outside
// src/media" rule, and it is what keeps the decoder leak-free on every early
// return in the error paths below.

struct FormatContextCloser {
    void operator()(AVFormatContext* context) const noexcept {
        if (context != nullptr) {
            AVFormatContext* mutable_context = context;
            avformat_close_input(&mutable_context);
        }
    }
};

struct CodecContextDeleter {
    void operator()(AVCodecContext* context) const noexcept {
        if (context != nullptr) {
            AVCodecContext* mutable_context = context;
            avcodec_free_context(&mutable_context);
        }
    }
};

struct FrameDeleter {
    void operator()(AVFrame* frame) const noexcept {
        if (frame != nullptr) {
            AVFrame* mutable_frame = frame;
            av_frame_free(&mutable_frame);
        }
    }
};

struct PacketDeleter {
    void operator()(AVPacket* packet) const noexcept {
        if (packet != nullptr) {
            AVPacket* mutable_packet = packet;
            av_packet_free(&mutable_packet);
        }
    }
};

using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextCloser>;
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;

}  // namespace

class VideoDecoder::Impl {
public:
    FormatContextPtr format;
    CodecContextPtr codec;
    FramePtr frame;
    PacketPtr packet;
    StreamInfo stream_info;
    int stream_index = -1;

    /// True once a null packet has been pushed to drain the decoder's internal
    /// queue. Without this the last few frames of every file are lost -- codecs
    /// with B-frames hold frames back until told the stream has ended.
    bool draining = false;
    bool finished = false;
    std::int64_t frames_decoded = 0;

    /// Copies the current AVFrame into an owned, tightly packed VideoFrame.
    [[nodiscard]] Result<VideoFrame> take_current_frame() {
        const auto pixel_format = static_cast<AVPixelFormat>(frame->format);
        const char* format_name = av_get_pix_fmt_name(pixel_format);
        if (format_name == nullptr) {
            return Error{Errc::unsupported_format, "decoder produced an unnamed pixel format"};
        }

        const int size = av_image_get_buffer_size(pixel_format, frame->width, frame->height, 1);
        if (size <= 0) {
            return detail::from_libav(size, "cannot size decoded frame buffer");
        }

        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size));
        // Alignment 1 means tightly packed with no row padding, which is what
        // makes frame_hash reproducible across platforms and decoder builds.
        const int copied = av_image_copy_to_buffer(
            pixels.data(), size, static_cast<const std::uint8_t* const*>(frame->data),
            frame->linesize, pixel_format, frame->width, frame->height, 1);
        if (copied < 0) {
            return detail::from_libav(copied, "cannot copy decoded frame");
        }

        VideoFrame out{frame->width, frame->height, std::string{format_name}, std::move(pixels)};
        out.time_base = stream_info.time_base;
        out.frame_index = frames_decoded;

        // best_effort_timestamp reconciles pts and dts and is what playback
        // should trust; a raw pts is frequently absent on the first frames.
        const std::int64_t timestamp = frame->best_effort_timestamp != AV_NOPTS_VALUE
                                           ? frame->best_effort_timestamp
                                           : frame->pts;
        if (timestamp != AV_NOPTS_VALUE) {
            out.presentation_timestamp = timestamp;
        }
        return out;
    }

    /// Reads one packet and hands it to the decoder, or begins draining at end
    /// of input. Returns an error only for real failures; running out of input
    /// is an expected transition, not a fault.
    [[nodiscard]] Result<void> feed_decoder() {
        if (draining) {
            return ok();
        }

        const int read_result = av_read_frame(format.get(), packet.get());
        if (read_result == AVERROR_EOF) {
            const int flush_result = avcodec_send_packet(codec.get(), nullptr);
            if (flush_result < 0 && flush_result != AVERROR_EOF) {
                return detail::from_libav(flush_result, "cannot flush decoder");
            }
            draining = true;
            return ok();
        }
        if (read_result < 0) {
            return detail::from_libav(read_result, "cannot read packet");
        }

        // av_read_frame hands back a reference that must be released whichever
        // way the rest of this function exits.
        struct PacketUnreferencer {
            AVPacket* packet;
            ~PacketUnreferencer() { av_packet_unref(packet); }
        } unreference{packet.get()};

        if (packet->stream_index != stream_index) {
            return ok();  // Audio or subtitle packet; not ours.
        }

        const int send_result = avcodec_send_packet(codec.get(), packet.get());
        if (send_result < 0 && send_result != AVERROR(EAGAIN)) {
            return detail::from_libav(send_result, "decoder rejected packet");
        }
        return ok();
    }
};

VideoDecoder::VideoDecoder(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
VideoDecoder::VideoDecoder(VideoDecoder&&) noexcept = default;
VideoDecoder& VideoDecoder::operator=(VideoDecoder&&) noexcept = default;
VideoDecoder::~VideoDecoder() = default;

Result<VideoDecoder> VideoDecoder::open(const std::filesystem::path& path) {
    detail::quiet_libav_logging_once();

    std::error_code exists_error;
    if (!std::filesystem::exists(path, exists_error)) {
        return Error{Errc::not_found, "no such file: " + path.string()};
    }

    const std::u8string utf8_path = path.u8string();
    const char* path_bytes = reinterpret_cast<const char*>(utf8_path.c_str());

    AVFormatContext* raw_format = nullptr;
    const int open_result = avformat_open_input(&raw_format, path_bytes, nullptr, nullptr);
    if (open_result < 0) {
        return detail::from_libav(open_result, "cannot open " + path.string());
    }

    auto impl = std::make_unique<Impl>();
    impl->format.reset(raw_format);

    const int stream_info_result = avformat_find_stream_info(impl->format.get(), nullptr);
    if (stream_info_result < 0) {
        return detail::from_libav(stream_info_result, "cannot read streams in " + path.string());
    }

    const AVCodec* codec = nullptr;
    const int stream_index =
        av_find_best_stream(impl->format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (stream_index < 0) {
        return detail::from_libav(stream_index, "no usable video stream in " + path.string());
    }
    if (codec == nullptr) {
        return Error{Errc::unsupported_format,
                     "no decoder available for the video stream in " + path.string()};
    }
    impl->stream_index = stream_index;

    const AVStream* stream = impl->format->streams[static_cast<unsigned int>(stream_index)];
    impl->stream_info = detail::describe_stream(*stream);

    impl->codec.reset(avcodec_alloc_context3(codec));
    if (!impl->codec) {
        return Error{Errc::out_of_memory, "cannot allocate decoder context"};
    }

    const int params_result = avcodec_parameters_to_context(impl->codec.get(), stream->codecpar);
    if (params_result < 0) {
        return detail::from_libav(params_result, "cannot configure decoder");
    }
    impl->codec->pkt_timebase = stream->time_base;

    const int codec_open_result = avcodec_open2(impl->codec.get(), codec, nullptr);
    if (codec_open_result < 0) {
        return detail::from_libav(codec_open_result, "cannot open decoder");
    }

    impl->frame.reset(av_frame_alloc());
    impl->packet.reset(av_packet_alloc());
    if (!impl->frame || !impl->packet) {
        return Error{Errc::out_of_memory, "cannot allocate decoder working buffers"};
    }

    return VideoDecoder{std::move(impl)};
}

const StreamInfo& VideoDecoder::stream() const noexcept {
    return impl_->stream_info;
}

std::int64_t VideoDecoder::frames_decoded() const noexcept {
    return impl_->frames_decoded;
}

Result<std::optional<VideoFrame>> VideoDecoder::next_frame() {
    if (impl_->finished) {
        return std::optional<VideoFrame>{};
    }

    for (;;) {
        const int receive_result = avcodec_receive_frame(impl_->codec.get(), impl_->frame.get());
        if (receive_result == 0) {
            Result<VideoFrame> frame = impl_->take_current_frame();
            av_frame_unref(impl_->frame.get());
            if (!frame) {
                return frame.error();
            }
            ++impl_->frames_decoded;
            return std::optional<VideoFrame>{std::move(frame).value()};
        }

        if (receive_result == AVERROR_EOF) {
            // End of stream is an outcome, not a failure.
            impl_->finished = true;
            return std::optional<VideoFrame>{};
        }

        if (receive_result != AVERROR(EAGAIN)) {
            return detail::from_libav(receive_result, "decoder failed");
        }

        if (Result<void> fed = impl_->feed_decoder(); !fed) {
            return fed.error();
        }
    }
}

}  // namespace rf::media
