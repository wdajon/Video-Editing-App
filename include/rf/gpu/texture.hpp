// An image living in GPU memory.
//
// Exists because of a measurement. Compositing through CPU pixels in and CPU
// pixels out cost 49.9 ms per frame at 1080x1920 with three layers, against a
// 33.3 ms budget -- roughly 33 MB across PCIe every frame with no overlap, of
// which a fixed ~33 ms was readback and the fence stall (see D13 and the
// iteration 5 numbers in docs/PROGRESS.md).
//
// A Texture is uploaded when its contents change and stays on the device. A
// still layer is uploaded once and never again; a video layer is uploaded once
// per decoded frame. Nothing is read back unless something actually asks for
// pixels on the CPU, which playback never does.

#ifndef RF_GPU_TEXTURE_HPP
#define RF_GPU_TEXTURE_HPP

#include <memory>

#include "rf/core/result.hpp"
#include "rf/gpu/device.hpp"
#include "rf/gpu/image.hpp"

namespace rf::gpu {

class Texture {
public:
    /// Allocates an uninitialised texture. Its contents are undefined until
    /// something writes to it -- either an upload or a composite.
    [[nodiscard]] static Result<Texture> create(Device& device, int width, int height);

    /// Allocates and uploads in one step.
    [[nodiscard]] static Result<Texture> create_from(Device& device, const ImageRgba8& pixels);

    Texture(Texture&&) noexcept;
    Texture& operator=(Texture&&) noexcept;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    ~Texture();

    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;

    /// Replaces the contents. `pixels` must match the texture's size exactly.
    [[nodiscard]] Result<void> upload(const ImageRgba8& pixels);

    /// Copies the contents back to the CPU.
    ///
    /// For tests, golden frames and export. Deliberately explicit and
    /// deliberately not part of the playback loop: this is the single most
    /// expensive operation measured in iteration 5.
    [[nodiscard]] Result<ImageRgba8> read_back() const;

private:
    class Impl;
    explicit Texture(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;

    friend class Compositor;
};

}  // namespace rf::gpu

#endif  // RF_GPU_TEXTURE_HPP
