// CPU-side image, for readback and golden-frame comparison.

#ifndef RF_GPU_IMAGE_HPP
#define RF_GPU_IMAGE_HPP

#include <cstdint>
#include <vector>

namespace rf::gpu {

/// 8-bit RGBA, tightly packed, no row padding.
///
/// Tightly packed for the same reason VideoFrame is: a hash or a comparison
/// over padding bytes is a comparison over uninitialised memory that varies by
/// driver, which would produce mismatches indistinguishable from real ones.
struct ImageRgba8 {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;

    [[nodiscard]] std::size_t expected_size() const noexcept {
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    }

    [[nodiscard]] bool is_valid() const noexcept {
        return width > 0 && height > 0 && pixels.size() == expected_size();
    }

    friend bool operator==(const ImageRgba8&, const ImageRgba8&) = default;
};

/// What the bring-up pattern shader is defined to produce, computed on the CPU.
///
/// This is the reference the GPU output is compared against. It is written from
/// the shader's specification rather than derived from a captured GPU result,
/// so a wrong shader cannot quietly become the expected answer.
[[nodiscard]] ImageRgba8 expected_fill_pattern(int width, int height);

}  // namespace rf::gpu

#endif  // RF_GPU_IMAGE_HPP
