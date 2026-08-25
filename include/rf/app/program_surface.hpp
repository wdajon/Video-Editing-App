// A window that presents a composited texture, embedded in the widget tree.
//
// The Program panel painted through a readback: pixels came off the device so a
// QWidget could draw them. That costs roughly 36 ms per frame at 1080x1920 and
// is why the picture could not keep up with playback (D30).
//
// This is the other half of M3's presentation path, reused rather than rebuilt.
// Qt supplies only the surface (ADR 008); ReelForge keeps the instance, the
// device and the swapchain, and blits the composited texture into an acquired
// image under FIFO -- so the display sets the pace.
//
// It is a QWindow because it owns a Vulkan surface, and QWindows go into a
// widget layout through `QWidget::createWindowContainer`.

#ifndef RF_APP_PROGRAM_SURFACE_HPP
#define RF_APP_PROGRAM_SURFACE_HPP

#include <QVulkanInstance>
#include <QWindow>

#include <memory>
#include <optional>

#include "rf/core/result.hpp"
#include "rf/gpu/device.hpp"
#include "rf/gpu/instance.hpp"
#include "rf/gpu/swapchain.hpp"
#include "rf/gpu/texture.hpp"

namespace rf::app {

class ProgramSurface : public QWindow {
    Q_OBJECT

public:
    /// Borrows the instance and device the panel already owns. They must
    /// outlive this window.
    ProgramSurface(gpu::Instance& instance, gpu::Device& device);
    ~ProgramSurface() override;

    /// Adopts the Vulkan instance into Qt so it can make a surface for this
    /// window. Fails on a platform that cannot present, which is a reportable
    /// condition rather than a crash.
    [[nodiscard]] Result<void> initialise();

    /// Blits `frame` into an acquired swapchain image and presents it.
    ///
    /// Returns `Errc::unavailable`-shaped failures as ordinary results: the
    /// window may not be exposed yet, and a caller rendering ahead of the first
    /// expose is normal rather than wrong.
    [[nodiscard]] Result<void> present(gpu::Texture& frame);

    /// True once the swapchain exists, which needs the window to be exposed.
    [[nodiscard]] bool ready() const noexcept;

Q_SIGNALS:
    /// Emitted the first time the window is exposed and the swapchain is built,
    /// so the owner can push the current frame without waiting for playback.
    void became_ready();

protected:
    void exposeEvent(QExposeEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rf::app

#endif  // RF_APP_PROGRAM_SURFACE_HPP
