#include "rf/app/program_monitor.hpp"

#include <QExposeEvent>
#include <QResizeEvent>
#include <QVulkanInstance>

#include <chrono>
#include <utility>
#include <vector>

#include "rf/gpu/compositor.hpp"
#include "rf/gpu/device.hpp"
#include "rf/gpu/image.hpp"
#include "rf/gpu/instance.hpp"
#include "rf/gpu/swapchain.hpp"
#include "rf/gpu/texture.hpp"
#include "rf/media/convert.hpp"
#include "rf/media/decoder.hpp"
#include "rf/media/rational.hpp"
#include "rf/playback/clock.hpp"
#include "rf/playback/frame_log.hpp"
#include "rf/playback/pacer.hpp"
#include "rf/playback/playback_clock.hpp"

namespace rf::app {
namespace {

constexpr int kFrameWidth = 1080;
constexpr int kFrameHeight = 1920;
constexpr int kFrameRate = 30;

/// A solid layer, so there is something composited over the video rather than
/// the video alone -- the gate is three layers.
gpu::ImageRgba8 solid(int width, int height, std::uint8_t red, std::uint8_t green,
                      std::uint8_t blue, std::uint8_t alpha) {
    gpu::ImageRgba8 image;
    image.width = width;
    image.height = height;
    image.pixels.resize(image.expected_size());
    for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
        image.pixels[i + 0] = red;
        image.pixels[i + 1] = green;
        image.pixels[i + 2] = blue;
        image.pixels[i + 3] = alpha;
    }
    return image;
}

}  // namespace

class ProgramMonitor::Impl {
public:
    QVulkanInstance qt_instance;

    std::optional<gpu::Instance> instance;
    std::optional<gpu::Device> device;
    std::optional<gpu::Compositor> compositor;
    std::optional<gpu::Swapchain> swapchain;
    std::optional<gpu::Texture> target;
    std::vector<gpu::Texture> textures;
    std::vector<gpu::GpuLayer> layers;

    std::optional<media::VideoDecoder> decoder;

    playback::SteadyClock wall;
    std::optional<playback::PlaybackClock> playback;
    playback::FrameLog log{2048};
    std::optional<playback::Pacer> pacer;

    bool ready = false;
    bool finished = false;
};

ProgramMonitor::ProgramMonitor() : impl_(std::make_unique<Impl>()) {
    setSurfaceType(QSurface::VulkanSurface);
    setTitle(QStringLiteral("ReelForge — Program"));
    resize(540, 960);
}

ProgramMonitor::~ProgramMonitor() = default;

std::int64_t ProgramMonitor::presented_frames() const noexcept {
    return impl_->log.presented_count();
}

std::int64_t ProgramMonitor::dropped_frames() const noexcept {
    return impl_->log.dropped_count();
}

double ProgramMonitor::interval_p99_ms() const noexcept {
    const playback::FrameStatistics stats =
        impl_->log.statistics(playback::Nanoseconds{1'000'000'000LL / kFrameRate});
    return std::chrono::duration<double, std::milli>(stats.interval_p99).count();
}

Result<void> ProgramMonitor::initialise(const std::string& video_path) {
    gpu::Instance::Options options;
    options.enable_presentation = true;
#ifndef NDEBUG
    options.enable_validation = true;
#endif

    auto instance = gpu::Instance::create(options);
    if (!instance) {
        return instance.error();
    }
    if (!instance.value().presentation_supported()) {
        return Error{Errc::unsupported_format,
                     "this machine has no Vulkan surface support, so nothing can be displayed"};
    }
    impl_->instance = std::move(instance).value();

    auto device = gpu::Device::create_preferred(impl_->instance.value());
    if (!device) {
        return device.error();
    }
    impl_->device = std::move(device).value();

    auto compositor = gpu::Compositor::create(impl_->device.value());
    if (!compositor) {
        return compositor.error();
    }
    impl_->compositor = std::move(compositor).value();

    auto target = gpu::Texture::create(impl_->device.value(), kFrameWidth, kFrameHeight);
    if (!target) {
        return target.error();
    }
    impl_->target = std::move(target).value();

    // Layer 0 is the video if one was given, otherwise a flat colour.
    auto base = gpu::Texture::create_from(
        impl_->device.value(), solid(kFrameWidth, kFrameHeight, 20, 30, 45, 255));
    if (!base) {
        return base.error();
    }
    impl_->textures.push_back(std::move(base).value());

    auto tint = gpu::Texture::create_from(
        impl_->device.value(), solid(kFrameWidth, kFrameHeight, 200, 40, 40, 90));
    if (!tint) {
        return tint.error();
    }
    impl_->textures.push_back(std::move(tint).value());

    auto wash = gpu::Texture::create_from(
        impl_->device.value(), solid(kFrameWidth, kFrameHeight, 255, 255, 255, 40));
    if (!wash) {
        return wash.error();
    }
    impl_->textures.push_back(std::move(wash).value());

    impl_->layers.push_back({&impl_->textures[0], 1.0F, true});
    impl_->layers.push_back({&impl_->textures[1], 0.6F, true});
    impl_->layers.push_back({&impl_->textures[2], 0.5F, true});

    if (!video_path.empty()) {
        auto decoder = media::VideoDecoder::open(video_path);
        if (!decoder) {
            return decoder.error();
        }
        impl_->decoder = std::move(decoder).value();
    }

    auto playback = playback::PlaybackClock::create(media::Rational{kFrameRate, 1});
    if (!playback) {
        return playback.error();
    }
    impl_->playback = std::move(playback).value();

    // Qt turns our instance into a surface; it does not own anything else.
    //
    // The extension list has to be handed over explicitly. Qt cannot discover
    // what was enabled on a VkInstance it did not create, and without knowing
    // the platform surface extension is present its surface creation fails
    // inside a window callback -- which surfaces as a process-level crash with
    // no diagnostic, not a returned error.
    QByteArrayList qt_extensions;
    for (const std::string& name : impl_->instance.value().enabled_extensions()) {
        qt_extensions.append(QByteArray::fromStdString(name));
    }
    impl_->qt_instance.setExtensions(qt_extensions);
    impl_->qt_instance.setVkInstance(
        reinterpret_cast<VkInstance>(impl_->instance.value().native_handle()));
    if (!impl_->qt_instance.create()) {
        return Error{Errc::internal,
                     "Qt could not adopt the Vulkan instance (error " +
                         std::to_string(impl_->qt_instance.errorCode()) + ")"};
    }
    setVulkanInstance(&impl_->qt_instance);

    return ok();
}

void ProgramMonitor::exposeEvent(QExposeEvent* /*event*/) {
    if (!isExposed() || impl_->finished) {
        return;
    }

    if (!impl_->ready) {
        const auto surface = static_cast<gpu::SurfaceHandle>(
            reinterpret_cast<std::uintptr_t>(impl_->qt_instance.surfaceForWindow(this)));
        if (surface == 0) {
            qWarning("ReelForge: no Vulkan surface for this window");
            return;
        }

        auto swapchain = gpu::Swapchain::create(impl_->device.value(), surface,
                                                static_cast<int>(width()),
                                                static_cast<int>(height()));
        if (!swapchain) {
            qWarning("ReelForge: %s", swapchain.error().to_string().c_str());
            return;
        }
        impl_->swapchain = std::move(swapchain).value();

        if (auto played = impl_->playback->play(impl_->wall.now(), media::Rational{1, 1});
            !played) {
            qWarning("ReelForge: %s", played.error().to_string().c_str());
            return;
        }
        impl_->pacer.emplace(impl_->wall, impl_->playback.value(), impl_->log);
        impl_->ready = true;
    }

    requestUpdate();
}

void ProgramMonitor::resizeEvent(QResizeEvent* event) {
    if (impl_->swapchain.has_value() && event->size().width() > 0 &&
        event->size().height() > 0) {
        if (auto resized = impl_->swapchain->resize(event->size().width(),
                                                    event->size().height());
            !resized) {
            qWarning("ReelForge: %s", resized.error().to_string().c_str());
        }
    }
}

bool ProgramMonitor::event(QEvent* event) {
    if (event->type() == QEvent::UpdateRequest) {
        render_one_frame();
        return true;
    }
    return QWindow::event(event);
}

void ProgramMonitor::render_one_frame() {
    if (!impl_->ready || impl_->finished) {
        return;
    }

    auto tick = impl_->pacer->wait_next();
    if (!tick) {
        qWarning("ReelForge: %s", tick.error().to_string().c_str());
        return;
    }

    if (impl_->decoder.has_value()) {
        auto next = impl_->decoder->next_frame();
        if (!next) {
            qWarning("ReelForge: %s", next.error().to_string().c_str());
            impl_->finished = true;
            return;
        }
        if (next.value().has_value()) {
            auto rgba = media::to_rgba8(next.value().value());
            if (rgba) {
                gpu::ImageRgba8 image;
                image.width = rgba.value().width();
                image.height = rgba.value().height();
                image.pixels = rgba.value().pixels();
                if (image.width == kFrameWidth && image.height == kFrameHeight) {
                    if (auto uploaded = impl_->textures[0].upload(image); !uploaded) {
                        qWarning("ReelForge: %s", uploaded.error().to_string().c_str());
                    }
                }
            }
        } else {
            // End of the clip. Stop rather than looping, so the reported
            // statistics describe one honest pass over the source.
            impl_->finished = true;
            close();
            return;
        }
    }

    if (auto composed = impl_->compositor->composite_into(impl_->target.value(), impl_->layers);
        !composed) {
        qWarning("ReelForge: %s", composed.error().to_string().c_str());
        return;
    }

    Result<void> presented = impl_->swapchain->present(impl_->target.value());
    if (!presented && presented.error().code() == Errc::version_mismatch) {
        // Ordinary: the window was resized between acquire and present.
        if (auto rebuilt = impl_->swapchain->resize(static_cast<int>(width()),
                                                    static_cast<int>(height()));
            !rebuilt) {
            qWarning("ReelForge: %s", rebuilt.error().to_string().c_str());
        }
    } else if (!presented) {
        qWarning("ReelForge: %s", presented.error().to_string().c_str());
        impl_->finished = true;
        return;
    }

    impl_->pacer->presented(tick.value(), impl_->wall.now());
    requestUpdate();
}

}  // namespace rf::app
