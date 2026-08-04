#include "rf/media/probe.hpp"

#include <memory>
#include <string>
#include <utility>

#include "libav_error.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
}

namespace rf::media {
namespace {

/// Owns an AVFormatContext opened by avformat_open_input.
///
/// avformat_close_input takes a pointer-to-pointer and nulls it, so it cannot be
/// used as a unique_ptr deleter directly; this adapter is why there are no bare
/// avformat_close_input calls anywhere else.
struct FormatContextCloser {
    void operator()(AVFormatContext* context) const noexcept {
        if (context != nullptr) {
            AVFormatContext* mutable_context = context;
            avformat_close_input(&mutable_context);
        }
    }
};

using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextCloser>;

StreamKind classify(AVMediaType type) noexcept {
    switch (type) {
        case AVMEDIA_TYPE_VIDEO:      return StreamKind::video;
        case AVMEDIA_TYPE_AUDIO:      return StreamKind::audio;
        case AVMEDIA_TYPE_SUBTITLE:   return StreamKind::subtitle;
        case AVMEDIA_TYPE_DATA:       return StreamKind::data;
        case AVMEDIA_TYPE_ATTACHMENT: return StreamKind::attachment;
        case AVMEDIA_TYPE_UNKNOWN:
        case AVMEDIA_TYPE_NB:
        default:
            return StreamKind::unknown;
    }
}

/// AVRational -> Rational, dropping values the container did not really state.
///
/// Audio streams genuinely report 0/0 for frame rate, and some containers leave
/// aspect ratio at 0/1. Those are "not stated", not errors, and must not become
/// a plausible default.
std::optional<Rational> to_rational(AVRational value) {
    if (value.num == 0 || value.den == 0) {
        return std::nullopt;
    }
    Result<Rational> converted = Rational::from(value.num, value.den);
    if (!converted) {
        return std::nullopt;
    }
    return converted.value();
}

std::optional<std::int64_t> to_optional_timestamp(std::int64_t value) {
    return value == AV_NOPTS_VALUE ? std::nullopt : std::optional<std::int64_t>{value};
}

std::optional<std::int64_t> to_optional_positive(std::int64_t value) {
    return value > 0 ? std::optional<std::int64_t>{value} : std::nullopt;
}

std::string name_of(const AVCodecParameters& params) {
    const char* name = avcodec_get_name(params.codec_id);
    return name != nullptr ? std::string{name} : std::string{};
}

StreamInfo describe_stream(const AVStream& stream) {
    StreamInfo info;
    info.index = stream.index;

    const AVCodecParameters* params = stream.codecpar;
    if (params != nullptr) {
        info.kind = classify(params->codec_type);
        info.codec_name = name_of(*params);
    }

    // A zero time base means the container gave one we cannot use for timing.
    // Left as 0/1 rather than defaulted to something plausible, so callers can
    // detect it with is_zero() instead of silently computing wrong timestamps.
    if (std::optional<Rational> base = to_rational(stream.time_base); base.has_value()) {
        info.time_base = base.value();
    }

    info.duration = to_optional_timestamp(stream.duration);
    info.start_time = to_optional_timestamp(stream.start_time);
    info.container_frame_count = to_optional_positive(stream.nb_frames);

    if (params == nullptr) {
        return info;
    }

    if (info.kind == StreamKind::video) {
        VideoStreamInfo video;
        video.width = params->width;
        video.height = params->height;
        video.average_frame_rate = to_rational(stream.avg_frame_rate);
        video.real_frame_rate = to_rational(stream.r_frame_rate);
        video.sample_aspect_ratio = to_rational(params->sample_aspect_ratio);

        const char* pixel_format = av_get_pix_fmt_name(static_cast<AVPixelFormat>(params->format));
        if (pixel_format != nullptr) {
            video.pixel_format = pixel_format;
        }
        info.video = std::move(video);
    } else if (info.kind == StreamKind::audio) {
        AudioStreamInfo audio;
        audio.sample_rate = params->sample_rate;
        audio.channel_count = params->ch_layout.nb_channels;

        const char* sample_format = av_get_sample_fmt_name(static_cast<AVSampleFormat>(params->format));
        if (sample_format != nullptr) {
            audio.sample_format = sample_format;
        }
        info.audio = std::move(audio);
    }

    return info;
}

}  // namespace

Result<MediaInfo> probe_file(const std::filesystem::path& path) {
    detail::quiet_libav_logging_once();

    std::error_code exists_error;
    if (!std::filesystem::exists(path, exists_error)) {
        // Checked up front so the common case of a moved or renamed file gets a
        // clear message rather than whatever the demuxer happens to report.
        return Error{Errc::not_found, "no such file: " + path.string()};
    }

    // libav takes a UTF-8 byte path on every platform, including Windows where
    // std::filesystem::path is UTF-16 natively.
    const std::u8string utf8_path = path.u8string();
    const char* path_bytes = reinterpret_cast<const char*>(utf8_path.c_str());

    AVFormatContext* raw_context = nullptr;
    const int open_result = avformat_open_input(&raw_context, path_bytes, nullptr, nullptr);
    if (open_result < 0) {
        // avformat_open_input frees and nulls the context on failure; there is
        // nothing to release here.
        return detail::from_libav(open_result, "cannot open " + path.string());
    }

    FormatContextPtr context{raw_context};

    const int stream_info_result = avformat_find_stream_info(context.get(), nullptr);
    if (stream_info_result < 0) {
        return detail::from_libav(stream_info_result, "cannot read streams in " + path.string());
    }

    MediaInfo info;
    if (context->iformat != nullptr && context->iformat->name != nullptr) {
        info.format_name = context->iformat->name;
    }
    info.duration_microseconds = to_optional_timestamp(context->duration);
    info.bit_rate = to_optional_positive(context->bit_rate);

    info.streams.reserve(context->nb_streams);
    for (unsigned int i = 0; i < context->nb_streams; ++i) {
        const AVStream* stream = context->streams[i];
        if (stream != nullptr) {
            info.streams.push_back(describe_stream(*stream));
        }
    }

    if (info.streams.empty()) {
        return Error{Errc::corrupt_data, "no streams found in " + path.string()};
    }

    return info;
}

}  // namespace rf::media
