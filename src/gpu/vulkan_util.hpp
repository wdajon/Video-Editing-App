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

/// Creates a device-local RGBA8 image plus its memory and view.
[[nodiscard]] Result<void> create_storage_image(VkDevice device, VkPhysicalDevice physical,
                                                std::uint32_t width, std::uint32_t height,
                                                VkImageUsageFlags usage, VkImage& image,
                                                VkDeviceMemory& memory, VkImageView& view);

/// Creates a host-visible, host-coherent buffer plus its memory.
[[nodiscard]] Result<void> create_host_buffer(VkDevice device, VkPhysicalDevice physical,
                                              VkDeviceSize size, VkBufferUsageFlags usage,
                                              VkBuffer& buffer, VkDeviceMemory& memory);

/// Records a layout transition. Kept in one place because getting the access
/// masks wrong produces corruption that only shows on some drivers.
void image_barrier(VkCommandBuffer commands, VkImage image, VkImageLayout from, VkImageLayout to,
                   VkAccessFlags source_access, VkAccessFlags destination_access,
                   VkPipelineStageFlags source_stage, VkPipelineStageFlags destination_stage);

}  // namespace rf::gpu::detail

#endif  // RF_GPU_VULKAN_UTIL_HPP
