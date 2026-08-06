// Putting composited frames on a screen.
//
// The compositor writes into a Texture and knows nothing about this class; the
// swapchain copies that texture into whichever image it acquired. Keeping them
// separate is what stops "what you see" and "what you export" from diverging,
// since both paths are identical up to the final copy (ADR 008).

#ifndef RF_GPU_SWAPCHAIN_HPP
#define RF_GPU_SWAPCHAIN_HPP

#include <cstdint>
#include <memory>

#include "rf/core/result.hpp"
#include "rf/gpu/device.hpp"
#include "rf/gpu/texture.hpp"

namespace rf::gpu {

/// An opaque `VkSurfaceKHR`, passed as an integer so this header stays free of
/// Vulkan types.
///
/// The window layer obtains one from Qt and hands it straight through without
/// interpreting it. That is the single documented exception to ADR 007's rule
/// that nothing above rf_gpu touches Vulkan: `QVulkanInstance::surfaceForWindow`
/// returns the type, so the alternative would be rf_gpu depending on Qt, which
/// is worse.
using SurfaceHandle = std::uint64_t;

class Swapchain {
public:
    /// Creates a swapchain for `surface` at the given pixel size.
    ///
    /// Fails if the device's queue cannot present to this surface, which is
    /// rare but real -- some drivers expose a compute-only queue family that
    /// cannot.
    [[nodiscard]] static Result<Swapchain> create(Device& device, SurfaceHandle surface,
                                                  int width, int height);

    Swapchain(Swapchain&&) noexcept;
    Swapchain& operator=(Swapchain&&) noexcept;
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    ~Swapchain();

    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;
    [[nodiscard]] std::uint32_t image_count() const noexcept;

    /// Copies `source` to the screen, scaling if the window and the frame are
    /// different sizes.
    ///
    /// Blocks until the display is ready to accept a frame. With FIFO present
    /// mode that is the vsync wait, which is a more accurate pace than sleeping
    /// on a clock and lets the driver schedule the next frame (ADR 008).
    ///
    /// Returns `Errc::version_mismatch` when the surface has changed size or
    /// been invalidated: the caller should call `resize()` and try again. That
    /// is an ordinary event -- a user dragging a window edge produces it -- not
    /// a failure.
    [[nodiscard]] Result<void> present(Texture& source);

    /// Rebuilds for a new window size. Safe to call when nothing has changed.
    [[nodiscard]] Result<void> resize(int width, int height);

private:
    class Impl;
    explicit Swapchain(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rf::gpu

#endif  // RF_GPU_SWAPCHAIN_HPP
