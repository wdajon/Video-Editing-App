#include "rf/core/error.hpp"

#include <string>

namespace rf {

std::string_view to_string(Errc code) noexcept {
    switch (code) {
        case Errc::invalid_argument:   return "invalid_argument";
        case Errc::not_found:          return "not_found";
        case Errc::io_failure:         return "io_failure";
        case Errc::permission_denied:  return "permission_denied";
        case Errc::unsupported_format: return "unsupported_format";
        case Errc::corrupt_data:       return "corrupt_data";
        case Errc::decode_failure:     return "decode_failure";
        case Errc::encode_failure:     return "encode_failure";
        case Errc::out_of_memory:      return "out_of_memory";
        case Errc::device_lost:        return "device_lost";
        case Errc::cancelled:          return "cancelled";
        case Errc::timeout:            return "timeout";
        case Errc::version_mismatch:   return "version_mismatch";
        case Errc::already_exists:     return "already_exists";
        case Errc::internal:           return "internal";
    }
    return "unknown";
}

std::string Error::to_string() const {
    std::string out;
    const std::string_view name = rf::to_string(code_);
    const std::string_view file = origin_.file_name() != nullptr ? origin_.file_name() : "<unknown>";
    const std::string line = std::to_string(origin_.line());

    out.reserve(name.size() + message_.size() + file.size() + line.size() + 8);
    out.append(name);
    out.append(": ");
    out.append(message_);
    out.append(" (at ");
    out.append(file);
    out.push_back(':');
    out.append(line);
    out.push_back(')');
    return out;
}

Error Error::with_context(std::string_view context) const {
    std::string combined;
    combined.reserve(context.size() + message_.size() + 2);
    combined.append(context);
    combined.append(": ");
    combined.append(message_);

    Error copy(*this);
    copy.message_ = std::move(combined);
    return copy;
}

}  // namespace rf
