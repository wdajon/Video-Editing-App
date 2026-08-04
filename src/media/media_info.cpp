#include "rf/media/media_info.hpp"

namespace rf::media {

std::string_view to_string(StreamKind kind) noexcept {
    switch (kind) {
        case StreamKind::video:      return "video";
        case StreamKind::audio:      return "audio";
        case StreamKind::subtitle:   return "subtitle";
        case StreamKind::data:       return "data";
        case StreamKind::attachment: return "attachment";
        case StreamKind::unknown:    return "unknown";
    }
    return "unknown";
}

const StreamInfo* MediaInfo::primary_video() const noexcept {
    for (const StreamInfo& stream : streams) {
        if (stream.kind == StreamKind::video && stream.video.has_value()) {
            return &stream;
        }
    }
    return nullptr;
}

const StreamInfo* MediaInfo::primary_audio() const noexcept {
    for (const StreamInfo& stream : streams) {
        if (stream.kind == StreamKind::audio && stream.audio.has_value()) {
            return &stream;
        }
    }
    return nullptr;
}

}  // namespace rf::media
