#include "rf/app/program_panel.hpp"

#include <QPainter>
#include <QVBoxLayout>
#include <QPaintEvent>

#include <optional>
#include <string>
#include <utility>

#include "rf/app/program_surface.hpp"
#include "rf/gpu/device.hpp"
#include "rf/gpu/instance.hpp"
#include "rf/media/media_info.hpp"
#include "rf/media/probe.hpp"
#include "rf/render/sequence_renderer.hpp"
#include "rf/timeline/composition.hpp"

namespace rf::app {

class ProgramPanel::Impl {
public:
    explicit Impl(timeline::Document& document) : document_(document) {}

    [[nodiscard]] bool ready() const noexcept { return renderer_.has_value(); }

    /// Brings up Vulkan and sizes the renderer, once, on the first frame that
    /// has a clip under it.
    ///
    /// Deferred rather than done in the constructor because the size comes from
    /// the media: `Document` carries a time base and a frame rate but no frame
    /// size (D31), so until it does, the first clip's source decides. That is
    /// what an editor offering to match a sequence to its first clip does, and
    /// it is a stopgap either way.
    [[nodiscard]] Result<void> ensure_renderer(std::int64_t frame) {
        if (renderer_) {
            return ok();
        }

        Result<std::vector<timeline::Layer>> visible = timeline::layers_at(document_, frame);
        if (!visible) {
            return visible.error();
        }
        if (visible.value().empty()) {
            return Error{Errc::not_found, "nothing under the playhead to size the sequence from"};
        }

        Result<media::MediaInfo> info = media::probe_file(visible.value().front().source);
        if (!info) {
            return info.error().with_context(visible.value().front().source);
        }
        const media::StreamInfo* video = info.value().primary_video();
        if (video == nullptr) {
            return Error{Errc::unsupported_format,
                         visible.value().front().source + " has no video stream"};
        }

        if (!device_) {
            // Ask for presentation, but do not require it. Presenting is about
            // 36 ms per frame cheaper than reading back at 1080x1920; a machine
            // that cannot present still renders perfectly well (ADR 008), and
            // the panel falls back to painting.
            gpu::Instance::Options wanted;
            wanted.enable_presentation = true;
            Result<gpu::Instance> instance = gpu::Instance::create(wanted);
            if (!instance) {
                instance = gpu::Instance::create(gpu::Instance::Options{});
            }
            if (!instance) {
                return instance.error();
            }
            instance_ = std::make_unique<gpu::Instance>(std::move(instance).value());

            Result<gpu::Device> device = gpu::Device::create_preferred(*instance_);
            if (!device) {
                return device.error();
            }
            device_ = std::make_unique<gpu::Device>(std::move(device).value());
        }

        if (!video->video) {
            return Error{Errc::unsupported_format,
                         visible.value().front().source + " reports no video geometry"};
        }
        Result<render::SequenceRenderer> renderer =
            render::SequenceRenderer::create(*device_, video->video->width, video->video->height);
        if (!renderer) {
            return renderer.error();
        }
        renderer_.emplace(std::move(renderer).value());
        return ok();
    }

    [[nodiscard]] Result<std::optional<gpu::ImageRgba8>> render(std::int64_t frame) {
        if (Result<void> ready_now = ensure_renderer(frame); !ready_now) {
            return ready_now.error();
        }
        Result<const gpu::Texture*> texture = renderer_->render_to_texture(document_, frame);
        if (!texture) {
            return texture.error();
        }

        if (surface_ != nullptr && surface_->ready()) {
            // const_cast is confined to here. render_to_texture hands back a
            // const view because callers must not keep or mutate the target,
            // and presenting needs a non-const reference to blit from; widening
            // the return type would let every caller do more than present.
            auto& presentable = const_cast<gpu::Texture&>(*texture.value());
            if (Result<void> shown = surface_->present(presentable); !shown) {
                return shown.error();
            }
            return std::optional<gpu::ImageRgba8>{};
        }

        Result<gpu::ImageRgba8> pixels = texture.value()->read_back();
        if (!pixels) {
            return pixels.error();
        }
        return std::optional<gpu::ImageRgba8>{std::move(pixels).value()};
    }

    [[nodiscard]] gpu::Instance* instance() const noexcept { return instance_.get(); }
    [[nodiscard]] gpu::Device* device() const noexcept { return device_.get(); }
    void adopt_surface(ProgramSurface* surface) noexcept { surface_ = surface; }
    [[nodiscard]] bool presenting() const noexcept {
        return surface_ != nullptr && surface_->ready();
    }

private:
    timeline::Document& document_;
    std::unique_ptr<gpu::Instance> instance_;
    std::unique_ptr<gpu::Device> device_;
    std::optional<render::SequenceRenderer> renderer_;
    ProgramSurface* surface_ = nullptr;  // owned by the panel, not by this
};

ProgramPanel::ProgramPanel(timeline::Document& document, QWidget* parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(document)) {
    setObjectName("rf_panel_program");
    setMinimumSize(160, 120);
    setAutoFillBackground(true);
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
}

bool ProgramPanel::is_presenting() const noexcept {
    return impl_->presenting();
}

void ProgramPanel::attach_surface() {
    if (surface_ != nullptr || impl_->instance() == nullptr || impl_->device() == nullptr) {
        return;
    }
    auto surface = std::make_unique<ProgramSurface>(*impl_->instance(), *impl_->device());
    if (Result<void> started = surface->initialise(); !started) {
        // No presentation here. The readback path already works, so saying this
        // in the status line would be noise: the user sees a picture either way,
        // it is only slower.
        return;
    }
    surface_ = surface.release();
    container_ = QWidget::createWindowContainer(surface_, this);
    container_->setFocusPolicy(Qt::NoFocus);  // the Timeline keeps the keyboard
    layout_->addWidget(container_);
    impl_->adopt_surface(surface_);

    // The swapchain exists only once the window is exposed, so the frame already
    // showing has to be pushed again afterwards or the panel stays black until
    // the playhead next moves.
    connect(surface_, &ProgramSurface::became_ready, this, [this] {
        const std::int64_t showing = frame_;
        frame_ = -1;
        show_frame(showing);
    });
}

ProgramPanel::~ProgramPanel() = default;

bool ProgramPanel::can_render() const noexcept {
    return impl_->ready();
}

void ProgramPanel::show_frame(std::int64_t frame) {
    if (frame == frame_) {
        // A drag emits a move per pixel. Re-decoding the same frame for each
        // would make scrubbing crawl for no visible difference.
        return;
    }
    frame_ = frame;

    Result<std::optional<gpu::ImageRgba8>> image = impl_->render(frame);
    if (image && !image.value()) {
        // Presented straight to the screen; there is nothing to paint.
        status_.clear();
        picture_ = QImage{};
        return;
    }
    if (!image) {
        // Said out loud rather than shown as black: a monitor that goes dark
        // without explaining itself is indistinguishable from a broken one.
        picture_ = QImage{};
        status_ = QString::fromStdString(image.error().message());
        update();
        return;
    }

    status_.clear();
    const gpu::ImageRgba8& pixels = *image.value();
    // Copied, because the QImage would otherwise reference pixels owned by a
    // temporary that dies at the end of this statement.
    picture_ = QImage(pixels.pixels.data(), pixels.width, pixels.height, pixels.width * 4,
                      QImage::Format_RGBA8888)
                   .copy();
    update();
}

void ProgramPanel::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.fillRect(event->rect(), Qt::black);

    if (picture_.isNull()) {
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(rect(), Qt::AlignCenter,
                         status_.isEmpty() ? tr("No picture at the playhead") : status_);
        return;
    }

    // Letterboxed, never stretched: a monitor that changed the aspect ratio
    // would misrepresent the framing, which is the one thing it exists to show.
    const QSize fitted = picture_.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - fitted.width()) / 2, (height() - fitted.height()) / 2),
                       fitted);
    painter.drawImage(target, picture_);
}

}  // namespace rf::app
