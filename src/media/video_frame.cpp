#include "rf/media/video_frame.hpp"

#include <cstddef>
#include <cstdint>

namespace rf::media {
namespace {

constexpr std::uint64_t kFnvOffsetBasis = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

constexpr std::uint64_t fnv1a(std::uint64_t hash, const std::uint8_t* data,
                              std::size_t size) noexcept {
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<std::uint64_t>(data[i]);
        hash *= kFnvPrime;
    }
    return hash;
}

constexpr std::uint64_t fnv1a(std::uint64_t hash, std::int64_t value) noexcept {
    for (int shift = 0; shift < 64; shift += 8) {
        hash ^= static_cast<std::uint64_t>(value >> shift) & 0xFFull;
        hash *= kFnvPrime;
    }
    return hash;
}

}  // namespace

std::uint64_t frame_hash(const VideoFrame& frame) noexcept {
    // Geometry and format are folded in as well as the pixels: two frames whose
    // bytes coincide but whose dimensions differ are not the same frame, and a
    // hash that said otherwise would hide a real defect.
    std::uint64_t hash = kFnvOffsetBasis;
    hash = fnv1a(hash, static_cast<std::int64_t>(frame.width()));
    hash = fnv1a(hash, static_cast<std::int64_t>(frame.height()));
    hash = fnv1a(hash, reinterpret_cast<const std::uint8_t*>(frame.pixel_format().data()),
                 frame.pixel_format().size());
    hash = fnv1a(hash, frame.pixels().data(), frame.pixels().size());
    return hash;
}

}  // namespace rf::media
