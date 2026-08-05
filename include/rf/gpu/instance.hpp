// Vulkan instance and device enumeration.

#ifndef RF_GPU_INSTANCE_HPP
#define RF_GPU_INSTANCE_HPP

#include <memory>
#include <string_view>
#include <vector>

#include "rf/core/result.hpp"
#include "rf/gpu/device_info.hpp"

namespace rf::gpu {

/// True when a Vulkan loader could be found and initialised at all.
///
/// A machine with no driver is an ordinary situation -- a fresh VM, a broken
/// install, a CI runner -- and it must be reportable rather than fatal. Callers
/// use this to decide between "no GPU acceleration" and "something is wrong".
[[nodiscard]] bool vulkan_available() noexcept;

/// Owns the Vulkan instance. Move-only; all Vulkan types stay behind the pimpl.
class Instance {
public:
    struct Options {
        std::string_view application_name = "ReelForge";

        /// Validation layers catch the whole class of "works on my driver"
        /// defects. Requested in debug and sanitizer builds; their absence is
        /// not an error, because ADR 007 deliberately does not require the SDK.
        bool enable_validation = false;
    };

    [[nodiscard]] static Result<Instance> create(const Options& options);

    Instance(Instance&&) noexcept;
    Instance& operator=(Instance&&) noexcept;
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
    ~Instance();

    /// Every physical device the loader reports, usable or not, in the order
    /// the driver lists them.
    [[nodiscard]] Result<std::vector<DeviceInfo>> enumerate_devices() const;

    /// Index into enumerate_devices() of the device ReelForge would choose:
    /// discrete before integrated before software, and among equals the one
    /// with the most device-local memory. Fails when none is usable.
    [[nodiscard]] Result<std::size_t> preferred_device() const;

    /// True when validation layers were actually enabled, which is not the same
    /// as having asked for them.
    [[nodiscard]] bool validation_enabled() const noexcept;

private:
    class Impl;
    explicit Instance(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rf::gpu

#endif  // RF_GPU_INSTANCE_HPP
