#include "rf/gpu/image.hpp"

namespace rf::gpu {

ImageRgba8 expected_fill_pattern(int width, int height) {
    ImageRgba8 image;
    if (width <= 0 || height <= 0) {
        return image;
    }
    image.width = width;
    image.height = height;
    image.pixels.resize(image.expected_size());

    // Mirrors shaders/fill_pattern.comp exactly. The shader computes
    // float(v % 256) / 255.0 and stores to an rgba8 unorm image, which rounds
    // back to v % 256, so every channel is an exact integer round trip -- the
    // comparison can be equality rather than a tolerance.
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)) * 4u;
            image.pixels[offset + 0] = static_cast<std::uint8_t>(x % 256);
            image.pixels[offset + 1] = static_cast<std::uint8_t>(y % 256);
            image.pixels[offset + 2] = static_cast<std::uint8_t>((x + y) % 256);
            image.pixels[offset + 3] = 255u;
        }
    }
    return image;
}

}  // namespace rf::gpu
