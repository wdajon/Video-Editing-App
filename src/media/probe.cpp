#include "rf/media/probe.hpp"

#include <memory>
#include <string>

#include "libav_error.hpp"
#include "stream_description.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
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

std::optional<std::int64_t> to_optional_timestamp(std::int64_t value) {
    return value == AV_NOPTS_VALUE ? std::nullopt : std::optional<std::int64_t>{value};
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
    info.bit_rate = context->bit_rate > 0 ? std::optional<std::int64_t>{context->bit_rate}
                                          : std::nullopt;

    info.streams.reserve(context->nb_streams);
    for (unsigned int i = 0; i < context->nb_streams; ++i) {
        const AVStream* stream = context->streams[i];
        if (stream != nullptr) {
            info.streams.push_back(detail::describe_stream(*stream));
        }
    }

    if (info.streams.empty()) {
        return Error{Errc::corrupt_data, "no streams found in " + path.string()};
    }

    return info;
}

}  // namespace rf::media
