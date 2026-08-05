// GPU capability description, free of Vulkan types.
//
// Nothing above rf_gpu may include a Vulkan header (ADR 007), so what the rest
// of ReelForge learns about a device it learns through this. The Vulkan include
// directories are PRIVATE to rf_gpu, which makes that rule a build error rather
// than a review comment.

#ifndef RF_GPU_DEVICE_INFO_HPP
#define RF_GPU_DEVICE_INFO_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace rf::gpu {

enum class DeviceKind : std::uint8_t {
    discrete,    ///< A separate card. What a 1080x1920 three-layer composite wants.
    integrated,  ///< Shares system memory; usable, slower, and worth knowing about.
    virtualised,
    software,    ///< A CPU implementation such as lavapipe. Correct, and far too slow to play back.
    other,
};

[[nodiscard]] std::string_view to_string(DeviceKind kind) noexcept;

struct ApiVersion {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;

    [[nodiscard]] std::string to_string() const;

    friend bool operator==(const ApiVersion&, const ApiVersion&) = default;
};

struct DeviceInfo {
    std::string name;
    DeviceKind kind = DeviceKind::other;
    ApiVersion api_version;
    std::uint32_t vendor_id = 0;
    std::uint32_t device_id = 0;

    /// Bytes of memory local to the device. Zero when the device reports none,
    /// which is normal for software implementations.
    std::uint64_t device_local_memory = 0;

    /// Index of a queue family able to do compute and transfer work. Absent
    /// means the device cannot serve as a compositor, whatever else it can do.
    /// Graphics capability is deliberately not required: compositing is done
    /// with compute shaders, and demanding graphics would exclude devices that
    /// could do the work.
    bool has_compute_queue = false;
    std::uint32_t compute_queue_family = 0;

    /// True when this device meets ReelForge's minimum requirements. A device
    /// that does not is still listed -- a user whose only GPU is unsuitable
    /// deserves to be told which one and why, not shown an empty list.
    [[nodiscard]] bool is_usable() const noexcept;

    /// Human-readable reason it is unusable, or empty when it is usable.
    [[nodiscard]] std::string unusable_reason() const;
};

/// Minimum Vulkan version ReelForge requires. 1.1 is the floor for the
/// subgroup operations and memory model the compositor will rely on, and is
/// supported by every driver still receiving updates.
inline constexpr ApiVersion kMinimumApiVersion{1, 1, 0};

}  // namespace rf::gpu

#endif  // RF_GPU_DEVICE_INFO_HPP
