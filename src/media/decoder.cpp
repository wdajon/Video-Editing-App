#include "rf/media/decoder.hpp"

#include <cstdint>
#include <memory>
#include <optional>
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

    /// Index to assign to the next frame handed out. Distinct from
    /// frames_decoded, which counts work done: after a seek, position and
    /// work-done are no longer the same number.
    std::int64_t next_index = 0;

    /// A frame already decoded during a seek, waiting to be handed to the
    /// caller. Seeking has to decode forward past the keyframe to reach the
    /// target, and the frame it lands on must not be thrown away.
    std::optional<VideoFrame> pending;

    /// Counts calls to take_current_frame -- that is, full-frame copies out of
    /// libav. See VideoDecoder::frames_materialised.
    std::int64_t frames_materialised = 0;

    /// Copies the current AVFrame into an owned, tightly packed VideoFrame.
    [[nodiscard]] Result<VideoFrame> take_current_frame() {
        ++frames_materialised;
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

    /// Pulls the next frame out of the decoder, feeding it packets as needed.
    /// Carries no notion of position; index assignment belongs to the caller.
    [[nodiscard]] Result<std::optional<VideoFrame>> decode_next_raw() {
        if (finished) {
            return std::optional<VideoFrame>{};
        }

        for (;;) {
            const int receive_result = avcodec_receive_frame(codec.get(), frame.get());
            if (receive_result == 0) {
                Result<VideoFrame> decoded = take_current_frame();
                av_frame_unref(frame.get());
                if (!decoded) {
                    return decoded.error();
                }
                return std::optional<VideoFrame>{std::move(decoded).value()};
            }

            if (receive_result == AVERROR_EOF) {
                // End of stream is an outcome, not a failure.
                finished = true;
                return std::optional<VideoFrame>{};
            }

            if (receive_result != AVERROR(EAGAIN)) {
                return detail::from_libav(receive_result, "decoder failed");
            }

            if (Result<void> fed = feed_decoder(); !fed) {
                return fed.error();
            }
        }
    }

    /// Seeks to the keyframe at or before `target`, then decodes forward to the
    /// first frame at or after it, leaving that frame pending.
    [[nodiscard]] Result<void> seek_and_decode_to(std::int64_t target, std::int64_t index_at_target) {
        const int seek_result =
            av_seek_frame(format.get(), stream_index, target, AVSEEK_FLAG_BACKWARD);
        if (seek_result < 0) {
            return detail::from_libav(seek_result, "cannot seek");
        }

        // Without this the decoder keeps state from before the jump and emits
        // frames belonging to the previous position.
        avcodec_flush_buffers(codec.get());
        draining = false;
        finished = false;
        pending.reset();

        // The scan below deliberately does NOT go through decode_next_raw().
        //
        // Frames between the keyframe and the target must be decoded -- the
        // codec needs them -- but they are never handed to anyone, so copying
        // each one into a packed buffer is pure waste. At 4K that is 12.4 MB
        // per discarded frame, and a 250-frame GOP discards up to 249 of them,
        // which was over a gigabyte of memcpy per seek before this loop existed.
        // Only the frame actually being sought is materialised.
        for (;;) {
            const int receive_result = avcodec_receive_frame(codec.get(), frame.get());

            if (receive_result == 0) {
                const std::int64_t timestamp = frame->best_effort_timestamp != AV_NOPTS_VALUE
                                                   ? frame->best_effort_timestamp
                                                   : frame->pts;
                if (timestamp == AV_NOPTS_VALUE) {
                    av_frame_unref(frame.get());
                    return Error{Errc::corrupt_data,
                                 "cannot seek accurately: decoded frame has no timestamp"};
                }
                if (timestamp < target) {
                    av_frame_unref(frame.get());
                    continue;
                }

                Result<VideoFrame> decoded = take_current_frame();
                av_frame_unref(frame.get());
                if (!decoded) {
                    return decoded.error();
                }
                VideoFrame candidate = std::move(decoded).value();
                candidate.frame_index = index_at_target;
                pending = std::move(candidate);
                next_index = index_at_target + 1;
                return ok();
            }

            if (receive_result == AVERROR_EOF) {
                finished = true;
                return Error{Errc::not_found,
                             "seek target " + std::to_string(target) + " is past the last frame"};
            }
            if (receive_result != AVERROR(EAGAIN)) {
                return detail::from_libav(receive_result, "decoder failed while seeking");
            }
            if (Result<void> fed = feed_decoder(); !fed) {
                return fed.error();
            }
        }
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

    // Without this libav decodes on a single thread, which leaves most of the
    // machine idle and makes 4K seeking an order of magnitude slower than the
    // hardware allows. thread_count 0 asks libav to pick based on the CPU;
    // decoders that cannot honour a threading mode silently ignore it.
    impl->codec->thread_count = 0;
    impl->codec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

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

std::int64_t VideoDecoder::frames_materialised() const noexcept {
    return impl_->frames_materialised;
}

Result<std::optional<VideoFrame>> VideoDecoder::next_frame() {
    if (impl_->pending.has_value()) {
        VideoFrame frame = std::move(impl_->pending).value();
        impl_->pending.reset();
        ++impl_->frames_decoded;
        return std::optional<VideoFrame>{std::move(frame)};
    }

    Result<std::optional<VideoFrame>> decoded = impl_->decode_next_raw();
    if (!decoded) {
        return decoded.error();
    }
    if (!decoded.value().has_value()) {
        return std::optional<VideoFrame>{};
    }

    VideoFrame frame = std::move(decoded).value().value();
    frame.frame_index = impl_->next_index;
    ++impl_->next_index;
    ++impl_->frames_decoded;
    return std::optional<VideoFrame>{std::move(frame)};
}

Result<void> VideoDecoder::seek_to_timestamp(std::int64_t timestamp) {
    // Index is unknowable from a bare timestamp on variable-rate material, so
    // counting resumes from wherever the seek lands rather than claiming a
    // position the stream cannot justify.
    return impl_->seek_and_decode_to(timestamp, impl_->next_index);
}

Result<void> VideoDecoder::seek_to_frame(std::int64_t index) {
    if (index < 0) {
        return Error{Errc::invalid_argument,
                     "frame index must not be negative: " + std::to_string(index)};
    }

    const std::optional<VideoStreamInfo>& video = impl_->stream_info.video;
    if (!video.has_value() || !video->average_frame_rate.has_value()) {
        return Error{Errc::unsupported_format,
                     "stream states no frame rate, so frame indices have no meaning; "
                     "use seek_to_timestamp"};
    }
    if (impl_->stream_info.time_base.is_zero()) {
        return Error{Errc::corrupt_data, "stream has an unusable time base"};
    }

    // Frame N starts at N frame-durations after the stream's start. Computed
    // through exact rational arithmetic: doing this in floating point is
    // precisely how a seek lands one frame off on 29.97 material.
    Result<Rational> frame_duration = video->average_frame_rate->inverse();
    if (!frame_duration) {
        return frame_duration.error().with_context("seek_to_frame");
    }

    Result<std::int64_t> offset =
        rescale(index, frame_duration.value(), impl_->stream_info.time_base, Rounding::nearest);
    if (!offset) {
        return offset.error().with_context("seek_to_frame");
    }

    const std::int64_t start = impl_->stream_info.start_time.value_or(0);
    return impl_->seek_and_decode_to(start + offset.value(), index);
}

}  // namespace rf::media
