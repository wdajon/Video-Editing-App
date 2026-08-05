#include "vulkan_util.hpp"

#include <string>

namespace rf::gpu::detail {

Error from_vulkan(VkResult result, std::string_view context) {
    Errc code = Errc::internal;
    switch (result) {
        case VK_ERROR_OUT_OF_HOST_MEMORY:
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            code = Errc::out_of_memory;
            break;
        case VK_ERROR_DEVICE_LOST:
            code = Errc::device_lost;
            break;
        case VK_ERROR_INITIALIZATION_FAILED:
        case VK_ERROR_INCOMPATIBLE_DRIVER:
        case VK_ERROR_FEATURE_NOT_PRESENT:
        case VK_ERROR_FORMAT_NOT_SUPPORTED:
            code = Errc::unsupported_format;
            break;
        case VK_ERROR_LAYER_NOT_PRESENT:
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            code = Errc::not_found;
            break;
        default:
            break;
    }
    return Error{code, std::string{context} + " (VkResult " + std::to_string(result) + ")"};
}

Result<std::uint32_t> find_memory_type(VkPhysicalDevice physical, std::uint32_t type_bits,
                                       VkMemoryPropertyFlags wanted) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        const bool allowed = (type_bits & (1u << i)) != 0;
        const bool suitable = (properties.memoryTypes[i].propertyFlags & wanted) == wanted;
        if (allowed && suitable) {
            return i;
        }
    }
    return Error{Errc::unsupported_format, "no memory type satisfies the required properties"};
}

}  // namespace rf::gpu::detail
