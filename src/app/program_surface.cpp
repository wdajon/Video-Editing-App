#include "rf/app/program_surface.hpp"

#include <QByteArrayList>
#include <QExposeEvent>
#include <QResizeEvent>

#include <cstdint>
#include <string>

namespace rf::app {

class ProgramSurface::Impl {
public:
    Impl(gpu::Instance& instance, gpu::Device& device) : instance(instance), device(device) {}

    gpu::Instance& instance;
    gpu::Device& device;
    QVulkanInstance qt_instance;
    std::optional<gpu::Swapchain> swapchain;
    bool adopted = false;
};

ProgramSurface::ProgramSurface(gpu::Instance& instance, gpu::Device& device)
    : impl_(std::make_unique<Impl>(instance, device)) {
    setSurfaceType(QSurface::VulkanSurface);
}

ProgramSurface::~ProgramSurface() = default;

bool ProgramSurface::ready() const noexcept {
    return impl_->swapchain.has_value();
}

Result<void> ProgramSurface::initialise() {
    if (!impl_->instance.presentation_supported()) {
        return Error{Errc::unsupported_format,
                     "this Vulkan instance has no surface support, so nothing can be presented"};
    }

    // The extension list has to be handed over explicitly. Qt cannot discover
    // what was enabled on a VkInstance it did not create, and without knowing
    // the platform surface extension is present its surface creation fails
    // inside a window callback -- which surfaces as a process-level crash with
    // no diagnostic rather than a returned error. Learned in M3.
    QByteArrayList extensions;
    for (const std::string& name : impl_->instance.enabled_extensions()) {
        extensions.append(QByteArray::fromStdString(name));
    }
    impl_->qt_instance.setExtensions(extensions);
    impl_->qt_instance.setVkInstance(
        reinterpret_cast<VkInstance>(impl_->instance.native_handle()));
    if (!impl_->qt_instance.create()) {
        return Error{Errc::internal, "Qt could not adopt the Vulkan instance (error " +
                                         std::to_string(impl_->qt_instance.errorCode()) + ")"};
    }
    setVulkanInstance(&impl_->qt_instance);
    impl_->adopted = true;
    return ok();
}

void ProgramSurface::exposeEvent(QExposeEvent* event) {
    QWindow::exposeEvent(event);
    if (!isExposed() || !impl_->adopted || impl_->swapchain.has_value()) {
        return;
    }

    const auto surface = static_cast<gpu::SurfaceHandle>(
        reinterpret_cast<std::uintptr_t>(impl_->qt_instance.surfaceForWindow(this)));
    if (surface == 0) {
        // A platform that cannot make a Vulkan surface for this window -- the
        // offscreen plugin, for one. The owner keeps its fallback; nothing here
        // pretends otherwise.
        return;
    }

    auto swapchain = gpu::Swapchain::create(impl_->device, surface, static_cast<int>(width()),
                                            static_cast<int>(height()));
    if (!swapchain) {
        qWarning("ReelForge: %s", swapchain.error().to_string().c_str());
        return;
    }
    impl_->swapchain = std::move(swapchain).value();
    Q_EMIT became_ready();
}

void ProgramSurface::resizeEvent(QResizeEvent* event) {
    QWindow::resizeEvent(event);
    if (!impl_->swapchain.has_value() || event->size().width() <= 0 ||
        event->size().height() <= 0) {
        return;
    }
    if (auto resized = impl_->swapchain->resize(event->size().width(), event->size().height());
        !resized) {
        qWarning("ReelForge: %s", resized.error().to_string().c_str());
    }
}

Result<void> ProgramSurface::present(gpu::Texture& frame) {
    if (!impl_->swapchain.has_value()) {
        return Error{Errc::not_found, "the window is not exposed yet"};
    }

    Result<void> presented = impl_->swapchain->present(frame);
    if (!presented && presented.error().code() == Errc::version_mismatch) {
        // Ordinary: the window was resized between acquire and present. Rebuild
        // and let the next frame land.
        if (auto rebuilt = impl_->swapchain->resize(static_cast<int>(width()),
                                                    static_cast<int>(height()));
            !rebuilt) {
            return rebuilt.error();
        }
        return ok();
    }
    return presented;
}

}  // namespace rf::app
