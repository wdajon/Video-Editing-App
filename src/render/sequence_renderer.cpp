#include "rf/render/sequence_renderer.hpp"

#include <map>
#include <utility>
#include <vector>

#include "rf/media/convert.hpp"
#include "rf/timeline/composition.hpp"

namespace rf::render {
namespace {

/// Turns a decoded RGBA frame into the compositor's image type.
[[nodiscard]] Result<gpu::ImageRgba8> to_image(const media::VideoFrame& frame, int width,
                                               int height) {
    if (frame.width() != width || frame.height() != height) {
        return Error{Errc::unsupported_format,
                     "source is " + std::to_string(frame.width()) + "x" +
                         std::to_string(frame.height()) + " but the sequence is " +
                         std::to_string(width) + "x" + std::to_string(height) +
                         "; scaling arrives with the transform system (M6)"};
    }
    gpu::ImageRgba8 image;
    image.width = width;
    image.height = height;
    image.pixels = frame.pixels();
    if (!image.is_valid()) {
        return Error{Errc::decode_failure, "decoded frame has the wrong number of bytes"};
    }
    return image;
}

}  // namespace

class SequenceRenderer::Impl {
public:
    Impl(gpu::Compositor compositor, int width, int height)
        : compositor_(std::move(compositor)), width_(width), height_(height) {}

    [[nodiscard]] Result<gpu::ImageRgba8> render(const timeline::Document& document,
                                                 std::int64_t frame,
                                                 const std::filesystem::path& media_root) {
        Result<std::vector<timeline::Layer>> visible = timeline::layers_at(document, frame);
        if (!visible) {
            return visible.error();
        }

        std::vector<gpu::Layer> layers;
        layers.reserve(visible.value().size());
        for (const timeline::Layer& layer : visible.value()) {
            Result<media::VideoDecoder*> decoder = decoder_for(layer.source, media_root);
            if (!decoder) {
                return decoder.error();
            }

            // Frame-accurate: the decoder seeks to the keyframe at or before the
            // target and decodes forward, so this is the exact frame rather than
            // the nearest one (M1).
            if (Result<void> sought = decoder.value()->seek_to_frame(layer.source_frame);
                !sought) {
                return sought.error().with_context(layer.source);
            }
            Result<std::optional<media::VideoFrame>> decoded = decoder.value()->next_frame();
            if (!decoded) {
                return decoded.error().with_context(layer.source);
            }
            if (!decoded.value()) {
                // The clip claims media the file does not have. Reported rather
                // than drawn as black, which would look like an intentional gap.
                return Error{Errc::decode_failure,
                             layer.source + " has no frame " +
                                 std::to_string(layer.source_frame)};
            }

            Result<media::VideoFrame> rgba = media::to_rgba8(decoded.value().value());
            if (!rgba) {
                return rgba.error().with_context(layer.source);
            }
            Result<gpu::ImageRgba8> image = to_image(rgba.value(), width_, height_);
            if (!image) {
                return image.error().with_context(layer.source);
            }

            gpu::Layer composited;
            composited.source = std::move(image).value();
            layers.push_back(std::move(composited));
        }

        // No layers composites to opaque black, which is what a gap looks like.
        return compositor_.composite(layers, width_, height_);
    }

    [[nodiscard]] std::size_t open_sources() const noexcept { return decoders_.size(); }

private:
    /// Decoders are kept open between frames. Reopening a file per frame would
    /// make scrubbing unusable and would show up as nothing but slowness.
    [[nodiscard]] Result<media::VideoDecoder*> decoder_for(const std::string& source,
                                                           const std::filesystem::path& root) {
        if (const auto found = decoders_.find(source); found != decoders_.end()) {
            return &found->second;
        }
        std::filesystem::path path(source);
        if (path.is_relative() && !root.empty()) {
            path = root / path;
        }
        Result<media::VideoDecoder> opened = media::VideoDecoder::open(path);
        if (!opened) {
            return opened.error().with_context(source);
        }
        const auto inserted = decoders_.emplace(source, std::move(opened).value());
        return &inserted.first->second;
    }

    gpu::Compositor compositor_;
    std::map<std::string, media::VideoDecoder> decoders_;
    int width_;
    int height_;
};

Result<SequenceRenderer> SequenceRenderer::create(gpu::Device& device, int width, int height) {
    if (width <= 0 || height <= 0) {
        return Error{Errc::invalid_argument, "sequence size must be positive"};
    }
    Result<gpu::Compositor> compositor = gpu::Compositor::create(device);
    if (!compositor) {
        return compositor.error();
    }
    return SequenceRenderer(
        std::make_unique<Impl>(std::move(compositor).value(), width, height));
}

SequenceRenderer::SequenceRenderer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SequenceRenderer::SequenceRenderer(SequenceRenderer&&) noexcept = default;
SequenceRenderer& SequenceRenderer::operator=(SequenceRenderer&&) noexcept = default;
SequenceRenderer::~SequenceRenderer() = default;

Result<gpu::ImageRgba8> SequenceRenderer::render(const timeline::Document& document,
                                                 std::int64_t frame,
                                                 const std::filesystem::path& media_root) {
    return impl_->render(document, frame, media_root);
}

std::size_t SequenceRenderer::open_sources() const noexcept {
    return impl_->open_sources();
}

}  // namespace rf::render
