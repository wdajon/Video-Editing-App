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

/// A decoder and where it is standing.
///
/// The position is what makes playing forward cheap: a decoder already holding
/// frame N needs no seek to produce N.
struct OpenSource {
    media::VideoDecoder decoder;
    std::int64_t next_source_frame = -1;
    /// Render pass this was last used in, for eviction.
    std::uint64_t touched = 0;
};

constexpr std::int64_t kUnknownPosition = -1;

/// How many decoders to keep open.
///
/// Decoders are keyed by *clip*, not by file: two clips showing the same file at
/// different frames need their own positions, or one of them forces a seek on
/// every frame -- measured at p50 23.5 ms each at 1080x1920. Keying by clip
/// means a timeline could accumulate one decoder per clip it has ever shown, so
/// the least recently used are dropped. Eight is comfortably more than the
/// number of layers any frame stacks and small enough that the open file handles
/// stay unremarkable.
constexpr std::size_t kMaxOpenDecoders = 8;

}  // namespace

class SequenceRenderer::Impl {
public:
    Impl(gpu::Device& device, gpu::Compositor compositor, gpu::Texture target, int width,
         int height)
        : device_(device),
          compositor_(std::move(compositor)),
          target_(std::move(target)),
          width_(width),
          height_(height) {}

    [[nodiscard]] Result<const gpu::Texture*> render_to_texture(
        const timeline::Document& document, std::int64_t frame,
        const std::filesystem::path& media_root) {
        ++pass_;
        Result<std::vector<timeline::Layer>> visible = timeline::layers_at(document, frame);
        if (!visible) {
            return visible.error();
        }

        // Textures are created once and reused. Allocating one per frame would
        // put a device allocation on the playback path, which the mission's
        // budget forbids outright.
        while (uploads_.size() < visible.value().size()) {
            Result<gpu::Texture> texture = gpu::Texture::create(device_, width_, height_);
            if (!texture) {
                return texture.error();
            }
            uploads_.push_back(std::move(texture).value());
        }

        std::vector<gpu::GpuLayer> layers;
        layers.reserve(visible.value().size());
        std::size_t slot = 0;
        for (const timeline::Layer& layer : visible.value()) {
            Result<OpenSource*> decoder = decoder_for(layer.clip, layer.source, media_root);
            if (!decoder) {
                return decoder.error();
            }

            OpenSource& open = *decoder.value();

            // Seek only when the frame wanted is not the one the decoder is
            // already standing on.
            //
            // A seek is frame-accurate -- it lands on the keyframe at or before
            // the target and decodes forward (M1) -- and that is exactly why it
            // is expensive: measured at p50 23.5 ms per frame at 1080x1920,
            // against a 33.33 ms budget for the whole frame. Playing forward
            // asks for consecutive frames, and for those the decoder is already
            // in the right place.
            if (open.next_source_frame != layer.source_frame) {
                if (Result<void> sought = open.decoder.seek_to_frame(layer.source_frame);
                    !sought) {
                    open.next_source_frame = kUnknownPosition;
                    return sought.error().with_context(layer.source);
                }
                ++seeks_;
            }
            // Unknown until the decode below succeeds: a failure part-way leaves
            // the decoder somewhere this cannot predict, and guessing would show
            // the wrong picture rather than merely being slow.
            open.next_source_frame = kUnknownPosition;

            Result<std::optional<media::VideoFrame>> decoded = open.decoder.next_frame();
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
            open.next_source_frame = layer.source_frame + 1;

            Result<media::VideoFrame> rgba = media::to_rgba8(decoded.value().value());
            if (!rgba) {
                return rgba.error().with_context(layer.source);
            }
            Result<gpu::ImageRgba8> image = to_image(rgba.value(), width_, height_);
            if (!image) {
                return image.error().with_context(layer.source);
            }

            if (Result<void> uploaded = uploads_[slot].upload(image.value()); !uploaded) {
                return uploaded.error().with_context(layer.source);
            }
            layers.push_back(gpu::GpuLayer{&uploads_[slot], 1.0F, true});
            ++slot;
        }

        // No layers composites to opaque black, which is what a gap looks like.
        if (Result<void> composed = compositor_.composite_into(target_, layers); !composed) {
            return composed.error();
        }
        return &target_;
    }

    [[nodiscard]] Result<gpu::ImageRgba8> render(const timeline::Document& document,
                                                 std::int64_t frame,
                                                 const std::filesystem::path& media_root) {
        Result<const gpu::Texture*> texture = render_to_texture(document, frame, media_root);
        if (!texture) {
            return texture.error();
        }
        return texture.value()->read_back();
    }

    [[nodiscard]] std::size_t open_sources() const noexcept { return decoders_.size(); }

    [[nodiscard]] std::int64_t seeks() const noexcept { return seeks_; }

    [[nodiscard]] std::int64_t frames_materialised() const noexcept {
        std::int64_t total = 0;
        for (const auto& [path, open] : decoders_) {
            total += open.decoder.frames_decoded();
        }
        return total;
    }

private:
    /// Decoders are kept open between frames. Reopening a file per frame would
    /// make scrubbing unusable and would show up as nothing but slowness.
    [[nodiscard]] Result<OpenSource*> decoder_for(timeline::ClipId clip,
                                                   const std::string& source,
                                                   const std::filesystem::path& root) {
        if (const auto found = decoders_.find(clip.value()); found != decoders_.end()) {
            found->second.touched = pass_;
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
        evict_if_crowded();
        const auto inserted = decoders_.emplace(
            clip.value(), OpenSource{std::move(opened).value(), kUnknownPosition, pass_});
        return &inserted.first->second;
    }

    /// Drops the least recently used decoder while the cache is over its cap.
    void evict_if_crowded() {
        while (decoders_.size() >= kMaxOpenDecoders) {
            auto oldest = decoders_.begin();
            for (auto it = decoders_.begin(); it != decoders_.end(); ++it) {
                if (it->second.touched < oldest->second.touched) {
                    oldest = it;
                }
            }
            decoders_.erase(oldest);
        }
    }

    gpu::Device& device_;
    gpu::Compositor compositor_;
    gpu::Texture target_;
    std::vector<gpu::Texture> uploads_;
    /// Keyed by clip id. See kMaxOpenDecoders for why not by file.
    std::map<std::uint64_t, OpenSource> decoders_;
    std::int64_t seeks_ = 0;
    std::uint64_t pass_ = 0;
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
    Result<gpu::Texture> target = gpu::Texture::create(device, width, height);
    if (!target) {
        return target.error();
    }
    return SequenceRenderer(std::make_unique<Impl>(device, std::move(compositor).value(),
                                                   std::move(target).value(), width, height));
}

SequenceRenderer::SequenceRenderer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SequenceRenderer::SequenceRenderer(SequenceRenderer&&) noexcept = default;
SequenceRenderer& SequenceRenderer::operator=(SequenceRenderer&&) noexcept = default;
SequenceRenderer::~SequenceRenderer() = default;

Result<const gpu::Texture*> SequenceRenderer::render_to_texture(
    const timeline::Document& document, std::int64_t frame,
    const std::filesystem::path& media_root) {
    return impl_->render_to_texture(document, frame, media_root);
}

Result<gpu::ImageRgba8> SequenceRenderer::render(const timeline::Document& document,
                                                 std::int64_t frame,
                                                 const std::filesystem::path& media_root) {
    return impl_->render(document, frame, media_root);
}

std::size_t SequenceRenderer::open_sources() const noexcept {
    return impl_->open_sources();
}

std::int64_t SequenceRenderer::frames_materialised() const noexcept {
    return impl_->frames_materialised();
}

std::int64_t SequenceRenderer::seeks() const noexcept {
    return impl_->seeks();
}

}  // namespace rf::render
