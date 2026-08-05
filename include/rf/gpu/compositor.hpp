// Layer compositing on the GPU.
//
// Two entry points into the same pipeline, for two different jobs:
//
//   composite_into()  GPU textures in, GPU texture out. Nothing crosses PCIe
//                     that does not have to. This is the playback path.
//   composite()       CPU pixels in, CPU pixels out. Convenient, correct, and
//                     measured at 49.9 ms per frame at 1080x1920 with three
//                     layers -- see D13. This is the export and golden-frame
//                     path, where latency does not matter.
//
// composite() is implemented on top of composite_into(), so the two cannot
// disagree about what a composite means.

#ifndef RF_GPU_COMPOSITOR_HPP
#define RF_GPU_COMPOSITOR_HPP

#include <memory>
#include <vector>

#include "rf/core/result.hpp"
#include "rf/gpu/device.hpp"
#include "rf/gpu/image.hpp"
#include "rf/gpu/texture.hpp"

namespace rf::gpu {

/// One layer of a composite, in bottom-to-top order, held on the CPU.
struct Layer {
    /// Pixels to draw. Must match the output size exactly -- scaling and
    /// placement are transform work that belongs with the keyframe system
    /// (M6), not here, and silently stretching would hide a caller's mistake.
    ImageRgba8 source;

    /// 0 hides the layer, 1 draws it at full strength. Multiplies the source
    /// alpha rather than replacing it.
    float opacity = 1.0F;

    /// A disabled layer is skipped entirely.
    bool enabled = true;
};

/// The same, but already resident on the device.
struct GpuLayer {
    /// Borrowed, not owned. Must outlive the composite call and match the
    /// target's size.
    const Texture* source = nullptr;
    float opacity = 1.0F;
    bool enabled = true;
};

/// Composites layers over an opaque black backdrop.
///
/// Owns its pipeline, so building one is paid for once rather than per frame.
/// Move-only, and tied to the Device it was created from.
class Compositor {
public:
    [[nodiscard]] static Result<Compositor> create(Device& device);

    Compositor(Compositor&&) noexcept;
    Compositor& operator=(Compositor&&) noexcept;
    Compositor(const Compositor&) = delete;
    Compositor& operator=(const Compositor&) = delete;
    ~Compositor();

    /// Blends `layers` bottom to top into `target`, entirely on the device.
    ///
    /// The playback path. No uploads, no readback: a still layer is uploaded
    /// once when it is created, a video layer once per decoded frame, and the
    /// result stays on the GPU for presentation.
    ///
    /// An empty list, or one where every layer is disabled, clears the target
    /// to opaque black -- what a timeline with nothing on it should show.
    [[nodiscard]] Result<void> composite_into(Texture& target,
                                              const std::vector<GpuLayer>& layers);

    /// Blends `layers` into a fresh `width` x `height` frame and returns it on
    /// the CPU.
    ///
    /// Uploads every layer and reads the result back on every call, which is
    /// why it is not the playback path. Correct, convenient, and the right
    /// shape for export and golden-frame comparison.
    [[nodiscard]] Result<ImageRgba8> composite(const std::vector<Layer>& layers, int width,
                                               int height);

private:
    class Impl;
    explicit Compositor(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

/// The same composite computed on the CPU, from the blend's definition.
///
/// This is the reference the GPU result is checked against. It is written from
/// the specification rather than captured from a GPU run, so a wrong shader
/// cannot become the expected answer.
[[nodiscard]] ImageRgba8 composite_reference(const std::vector<Layer>& layers, int width,
                                             int height);

}  // namespace rf::gpu

#endif  // RF_GPU_COMPOSITOR_HPP
