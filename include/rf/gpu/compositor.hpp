// Layer compositing on the GPU.
//
// M3's gate is three layers at 1080x1920. This is the piece that stacks them.

#ifndef RF_GPU_COMPOSITOR_HPP
#define RF_GPU_COMPOSITOR_HPP

#include <memory>
#include <vector>

#include "rf/core/result.hpp"
#include "rf/gpu/device.hpp"
#include "rf/gpu/image.hpp"

namespace rf::gpu {

/// One layer of a composite, in bottom-to-top order.
struct Layer {
    /// Pixels to draw. Must match the output size exactly -- scaling and
    /// placement are transform work that belongs with the keyframe system
    /// (M6), not here, and silently stretching would hide a caller's mistake.
    ImageRgba8 source;

    /// 0 hides the layer, 1 draws it at full strength. Multiplies the source
    /// alpha rather than replacing it.
    float opacity = 1.0F;

    /// A disabled layer is skipped entirely. Distinct from opacity 0, which
    /// still costs a dispatch -- the timeline model has an explicit enabled
    /// flag and this preserves that meaning.
    bool enabled = true;
};

/// Composites layers over an opaque black backdrop.
///
/// Owns its pipeline, so the cost of building one is paid once rather than per
/// frame. Move-only, and tied to the Device it was created from.
class Compositor {
public:
    [[nodiscard]] static Result<Compositor> create(Device& device);

    Compositor(Compositor&&) noexcept;
    Compositor& operator=(Compositor&&) noexcept;
    Compositor(const Compositor&) = delete;
    Compositor& operator=(const Compositor&) = delete;
    ~Compositor();

    /// Blends `layers` bottom to top into a `width` x `height` frame.
    ///
    /// An empty list, or one where every layer is disabled, yields opaque
    /// black -- which is what a timeline with nothing on it should show.
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
