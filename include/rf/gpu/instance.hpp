// Vulkan instance and device enumeration.

#ifndef RF_GPU_INSTANCE_HPP
#define RF_GPU_INSTANCE_HPP

#include <cstddef>
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

        /// Enables the surface extensions needed to put frames on a screen.
        ///
        /// Which ones exist is discovered from the loader rather than assumed
        /// per platform (ADR 008), so asking for presentation on a machine that
        /// cannot present is not an error -- the instance is created without
        /// them and `presentation_supported()` reports false. Export, golden
        /// frames and the benchmarks must keep working on such a machine.
        bool enable_presentation = false;
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

    /// True when the surface extensions needed for presentation were enabled.
    /// False on a headless machine, in CI, and whenever `enable_presentation`
    /// was not requested.
    [[nodiscard]] bool presentation_supported() const noexcept;

private:
    class Impl;
    explicit Instance(std::shared_ptr<Impl> impl) noexcept;

    // Shared, not unique. A VkDevice must not outlive the VkInstance it was
    // created from, and an API where that is merely documented is an API where
    // it happens: destroying an Instance while a Device from it was still alive
    // produced a hard crash the first time a test did exactly that. Device holds
    // a copy of this pointer, so the instance survives as long as any device
    // made from it.
    std::shared_ptr<Impl> impl_;

    // Device needs the native instance handle. Granting it here rather than
    // exposing an accessor keeps VkInstance out of this header entirely, which
    // is the whole point of the pimpl (ADR 007).
    friend class Device;
};

}  // namespace rf::gpu

#endif  // RF_GPU_INSTANCE_HPP
