// A logical GPU device and the headless compute path.
//
// Rendering is headless by design (ADR 007): the render graph produces images,
// and putting one on screen is a separate concern. That is what lets golden
// frames be compared in a unit test with no window, no surface, and no display
// server -- and it is the same path rf_render_headless will export through.

#ifndef RF_GPU_DEVICE_HPP
#define RF_GPU_DEVICE_HPP

#include <cstddef>
#include <memory>

#include "rf/core/result.hpp"
#include "rf/gpu/device_info.hpp"
#include "rf/gpu/image.hpp"
#include "rf/gpu/instance.hpp"

namespace rf::gpu {

class Device {
public:
    /// Opens the device at `device_index` in the instance's enumeration.
    /// Fails if the index is out of range or the device is unusable.
    [[nodiscard]] static Result<Device> create(const Instance& instance,
                                               std::size_t device_index);

    /// Opens whichever device `Instance::preferred_device()` chooses.
    [[nodiscard]] static Result<Device> create_preferred(const Instance& instance);

    Device(Device&&) noexcept;
    Device& operator=(Device&&) noexcept;
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    ~Device();

    [[nodiscard]] const DeviceInfo& info() const noexcept;

    /// Runs the bring-up pattern shader into a fresh image and reads it back.
    ///
    /// This exists to prove the whole compute path end to end -- device, memory,
    /// storage image, descriptors, pipeline, dispatch, barriers, submission and
    /// readback -- against a result that can be predicted exactly on the CPU.
    /// The compositor replaces the shader, not the path.
    [[nodiscard]] Result<ImageRgba8> render_fill_pattern(int width, int height);

private:
    class Impl;
    explicit Device(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rf::gpu

#endif  // RF_GPU_DEVICE_HPP
