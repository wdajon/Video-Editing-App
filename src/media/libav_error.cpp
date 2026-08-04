#include "libav_error.hpp"

#include <array>
#include <mutex>
#include <string>

extern "C" {
#include <libavutil/error.h>
#include <libavutil/log.h>
}

namespace rf::media::detail {
namespace {

Errc classify(int averror) noexcept {
    // libav mixes negated errno values with its own FFERRTAG constants, so both
    // families have to be matched.
    switch (averror) {
        case AVERROR_INVALIDDATA:
            return Errc::corrupt_data;
        case AVERROR_DEMUXER_NOT_FOUND:
        case AVERROR_MUXER_NOT_FOUND:
        case AVERROR_DECODER_NOT_FOUND:
        case AVERROR_ENCODER_NOT_FOUND:
        case AVERROR_PROTOCOL_NOT_FOUND:
        case AVERROR_FILTER_NOT_FOUND:
        case AVERROR_STREAM_NOT_FOUND:
        case AVERROR_BSF_NOT_FOUND:
            return Errc::unsupported_format;
        case AVERROR_EOF:
            return Errc::not_found;
        case AVERROR_EXIT:
            return Errc::cancelled;
        case AVERROR_BUG:
        case AVERROR_BUG2:
        case AVERROR_UNKNOWN:
            return Errc::internal;
        case AVERROR_PATCHWELCOME:
            return Errc::unsupported_format;
        default:
            break;
    }

    if (averror == AVERROR(ENOENT)) {
        return Errc::not_found;
    }
    if (averror == AVERROR(EACCES) || averror == AVERROR(EPERM)) {
        return Errc::permission_denied;
    }
    if (averror == AVERROR(ENOMEM)) {
        return Errc::out_of_memory;
    }
    if (averror == AVERROR(EIO)) {
        return Errc::io_failure;
    }
    if (averror == AVERROR(EINVAL)) {
        return Errc::invalid_argument;
    }
    if (averror == AVERROR(ENOSYS)) {
        return Errc::unsupported_format;
    }
    if (averror == AVERROR(EAGAIN)) {
        // Callers that care about EAGAIN must check for it before reaching here;
        // arriving with it is a control-flow bug, not a media problem.
        return Errc::internal;
    }
    return Errc::io_failure;
}

std::string describe(int averror) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
    if (av_strerror(averror, buffer.data(), buffer.size()) < 0) {
        return "unrecognised libav error";
    }
    return std::string{buffer.data()};
}

}  // namespace

Error from_libav(int averror, std::string_view context, std::source_location origin) {
    std::string message;
    message.reserve(context.size() + 48);
    message.append(context);
    message.append(": ");
    message.append(describe(averror));
    message.append(" (libav ");
    message.append(std::to_string(averror));
    message.push_back(')');
    return Error{classify(averror), std::move(message), origin};
}

void quiet_libav_logging_once() noexcept {
    static std::once_flag flag;
    std::call_once(flag, [] { av_log_set_level(AV_LOG_ERROR); });
}

}  // namespace rf::media::detail
