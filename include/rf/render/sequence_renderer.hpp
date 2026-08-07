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

    /// Renders `document` at `frame`.
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

private:
    class Impl;
    explicit SequenceRenderer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace rf::render

#endif  // RF_RENDER_SEQUENCE_RENDERER_HPP
