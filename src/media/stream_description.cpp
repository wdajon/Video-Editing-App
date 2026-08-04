#include "stream_description.hpp"

#include <string>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
}

namespace rf::media::detail {
namespace {

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

std::optional<std::int64_t> to_optional_timestamp(std::int64_t value) {
    return value == AV_NOPTS_VALUE ? std::nullopt : std::optional<std::int64_t>{value};
}

std::optional<std::int64_t> to_optional_positive(std::int64_t value) {
    return value > 0 ? std::optional<std::int64_t>{value} : std::nullopt;
}

}  // namespace

std::optional<Rational> to_rational(const AVRational& value) {
    if (value.num == 0 || value.den == 0) {
        return std::nullopt;
    }
    Result<Rational> converted = Rational::from(value.num, value.den);
    if (!converted) {
        return std::nullopt;
    }
    return converted.value();
}

StreamInfo describe_stream(const AVStream& stream) {
    StreamInfo info;
    info.index = stream.index;

    const AVCodecParameters* params = stream.codecpar;
    if (params != nullptr) {
        info.kind = classify(params->codec_type);
        const char* codec_name = avcodec_get_name(params->codec_id);
        if (codec_name != nullptr) {
            info.codec_name = codec_name;
        }
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

        const char* sample_format =
            av_get_sample_fmt_name(static_cast<AVSampleFormat>(params->format));
        if (sample_format != nullptr) {
            audio.sample_format = sample_format;
        }
        info.audio = std::move(audio);
    }

    return info;
}

}  // namespace rf::media::detail
