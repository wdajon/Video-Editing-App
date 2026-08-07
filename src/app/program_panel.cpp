#include "rf/app/program_panel.hpp"

#include <QPainter>
#include <QPaintEvent>

#include <optional>
#include <string>
#include <utility>

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
            // No presentation: this reads pixels back rather than owning a
            // surface, so asking for one would fail on a machine that can render
            // perfectly well (ADR 008).
            Result<gpu::Instance> instance = gpu::Instance::create(gpu::Instance::Options{});
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

    [[nodiscard]] Result<gpu::ImageRgba8> render(std::int64_t frame) {
        if (Result<void> ready_now = ensure_renderer(frame); !ready_now) {
            return ready_now.error();
        }
        return renderer_->render(document_, frame);
    }

private:
    timeline::Document& document_;
    std::unique_ptr<gpu::Instance> instance_;
    std::unique_ptr<gpu::Device> device_;
    std::optional<render::SequenceRenderer> renderer_;
};

ProgramPanel::ProgramPanel(timeline::Document& document, QWidget* parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(document)) {
    setObjectName("rf_panel_program");
    setMinimumSize(160, 120);
    setAutoFillBackground(true);
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

    Result<gpu::ImageRgba8> image = impl_->render(frame);
    if (!image) {
        // Said out loud rather than shown as black: a monitor that goes dark
        // without explaining itself is indistinguishable from a broken one.
        picture_ = QImage{};
        status_ = QString::fromStdString(image.error().message());
        update();
        return;
    }

    status_.clear();
    // Copied, because the QImage would otherwise reference pixels owned by a
    // temporary that dies at the end of this statement.
    picture_ = QImage(image.value().pixels.data(), image.value().width, image.value().height,
                      image.value().width * 4, QImage::Format_RGBA8888)
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
