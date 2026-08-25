// A timeline frame, as pixels.
//
// The first path in ReelForge from a document to a picture. `layers_at()` says
// what is visible; this decodes each layer's source frame, converts it, and
// composites them in order.
//
// It is the export path's core as much as the monitor's: M5 renders the same
// way, one frame at a time, into an encoder rather than onto a screen. Building
// it once means an exported file and a previewed frame cannot disagree about
// what the edit looks like.
//
// See docs/adr/018-composition.md.

#ifndef RF_RENDER_SEQUENCE_RENDERER_HPP
#define RF_RENDER_SEQUENCE_RENDERER_HPP

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include "rf/core/result.hpp"
#include "rf/gpu/compositor.hpp"
#include "rf/gpu/device.hpp"
#include "rf/gpu/image.hpp"
#include "rf/gpu/texture.hpp"
#include "rf/media/decoder.hpp"
#include "rf/timeline/document.hpp"

namespace rf::render {

class SequenceRenderer {
public:
    /// `width` and `height` are the sequence size. Every layer must already
    /// match it -- see `render()`.
    [[nodiscard]] static Result<SequenceRenderer> create(gpu::Device& device, int width,
                                                         int height);

    SequenceRenderer(const SequenceRenderer&) = delete;
    SequenceRenderer& operator=(const SequenceRenderer&) = delete;
    SequenceRenderer(SequenceRenderer&&) noexcept;
    SequenceRenderer& operator=(SequenceRenderer&&) noexcept;
    ~SequenceRenderer();

    /// Renders `document` at `frame` into a texture that stays on the device.
    ///
    /// Nothing crosses PCIe on the way out, which is what makes playback
    /// possible: the readback path below measured p50 65.3 ms at 1080x1920 with
    /// one layer against a 33.33 ms budget, and a counter confirmed it was
    /// decoding exactly one frame per frame -- so the cost was the transfers,
    /// not the decoding. Same finding as D13, in a different place.
    ///
    /// The texture is owned by the renderer and is reused for every frame, so
    /// the pointer is valid until the next call. Present it or composite from
    /// it; do not keep it.
    [[nodiscard]] Result<const gpu::Texture*> render_to_texture(
        const timeline::Document& document, std::int64_t frame,
        const std::filesystem::path& media_root = {});

    /// The same frame, read back to CPU pixels.
    ///
    /// Implemented on top of `render_to_texture`, so the picture a monitor
    /// presents and the picture an export writes cannot disagree -- the
    /// arrangement M3 settled on for `composite_into` and `composite`.
    ///
    /// A frame with nothing on it renders opaque black rather than failing or
    /// returning the previous picture: a gap in the timeline is black, and
    /// holding the last frame there would make a stale picture indistinguishable
    /// from a live one.
    ///
    /// Fails when a source cannot be opened or does not match the sequence size.
    /// Scaling is transform work that belongs with the keyframe system (M6), and
    /// stretching silently here would hide a mismatched import.
    ///
    /// `media_root` is prepended to relative clip sources. A clip's `source` is
    /// a path today and will be a media-pool key later (D14); this is the seam.
    [[nodiscard]] Result<gpu::ImageRgba8> render(const timeline::Document& document,
                                                 std::int64_t frame,
                                                 const std::filesystem::path& media_root = {});

    /// Decoders opened so far. Exposed because reopening a file per frame would
    /// make scrubbing unusable and would not show up as anything but slowness.
    [[nodiscard]] std::size_t open_sources() const noexcept;

    /// Frames the decoders have handed out, across every source.
    ///
    /// One per layer per rendered frame is the floor. **It does not report the
    /// cost of a seek**, and it was believed to for two iterations: a seek
    /// decodes forward from a keyframe without handing those frames out, so this
    /// reads a healthy 1.0 per frame whether or not 23.5 ms went into getting
    /// there. `seeks()` is the counter for that question. Kept because it still
    /// answers its own one -- whether a frame was delivered per frame asked for.
    [[nodiscard]] std::int64_t frames_decoded() const noexcept;

    /// Seeks performed. The counter that matters for playback: a seek is
    /// frame-accurate and therefore expensive (p50 23.5 ms at 1080x1920), and
    /// playing forward should need none after the first.
    ///
    /// Neither `frames_decoded` nor the decoder's own `frames_materialised` can
    /// answer this: a seek decodes forward from a keyframe and hands out one
    /// frame, so both read 1.0 per frame while 80% of the time disappears. A
    /// counter is only as good as the question it is asked.
    [[nodiscard]] std::int64_t seeks() const noexcept;

private:
    class Impl;
    explicit SequenceRenderer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace rf::render

#endif  // RF_RENDER_SEQUENCE_RENDERER_HPP
