// Internal Vulkan helpers shared inside rf_gpu. Includes volk, so nothing
// outside src/gpu/ may include it.

#ifndef RF_GPU_VULKAN_UTIL_HPP
#define RF_GPU_VULKAN_UTIL_HPP

#include <string_view>

#include <volk.h>

#include "rf/core/error.hpp"
#include "rf/core/result.hpp"

namespace rf::gpu::detail {

/// Translates a VkResult into an rf::Error, keeping the numeric code. Vulkan's
/// result names are not in the binary, so the number is what makes a user's bug
/// report actionable.
[[nodiscard]] Error from_vulkan(VkResult result, std::string_view context);

/// Finds a memory type satisfying both the resource's requirements and the
/// properties needed. There is no universal "device local" or "host visible"
/// index -- it varies per device, and hardcoding one is a bug that only appears
/// on somebody else's hardware.
[[nodiscard]] Result<std::uint32_t> find_memory_type(VkPhysicalDevice physical,
                                                     std::uint32_t type_bits,
                                                     VkMemoryPropertyFlags wanted);

}  // namespace rf::gpu::detail

#endif  // RF_GPU_VULKAN_UTIL_HPP
