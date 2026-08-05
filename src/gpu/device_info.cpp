#include "rf/gpu/device_info.hpp"

#include <string>

namespace rf::gpu {

std::string_view to_string(DeviceKind kind) noexcept {
    switch (kind) {
        case DeviceKind::discrete:    return "discrete";
        case DeviceKind::integrated:  return "integrated";
        case DeviceKind::virtualised: return "virtualised";
        case DeviceKind::software:    return "software";
        case DeviceKind::other:       return "other";
    }
    return "other";
}

std::string ApiVersion::to_string() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

namespace {

[[nodiscard]] bool at_least(const ApiVersion& value, const ApiVersion& minimum) noexcept {
    if (value.major != minimum.major) {
        return value.major > minimum.major;
    }
    if (value.minor != minimum.minor) {
        return value.minor > minimum.minor;
    }
    return value.patch >= minimum.patch;
}

}  // namespace

bool DeviceInfo::is_usable() const noexcept {
    return has_compute_queue && at_least(api_version, kMinimumApiVersion);
}

std::string DeviceInfo::unusable_reason() const {
    if (is_usable()) {
        return {};
    }
    if (!at_least(api_version, kMinimumApiVersion)) {
        return "reports Vulkan " + api_version.to_string() + ", but ReelForge needs " +
               kMinimumApiVersion.to_string() + " or newer";
    }
    return "has no queue family that can run compute work";
}

}  // namespace rf::gpu
