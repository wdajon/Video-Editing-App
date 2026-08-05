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

[[nodiscard]] Result<void> create_storage_image(VkDevice device, VkPhysicalDevice physical,
                                                std::uint32_t width, std::uint32_t height,
                                                VkImageUsageFlags usage, VkImage& image,
                                                VkDeviceMemory& memory, VkImageView& view) {
    VkImageCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create.imageType = VK_IMAGE_TYPE_2D;
    create.format = VK_FORMAT_R8G8B8A8_UNORM;
    create.extent = {width, height, 1};
    create.mipLevels = 1;
    create.arrayLayers = 1;
    create.samples = VK_SAMPLE_COUNT_1_BIT;
    create.tiling = VK_IMAGE_TILING_OPTIMAL;
    create.usage = usage;
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult result = vkCreateImage(device, &create, nullptr, &image);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot create a composite image");
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, image, &requirements);
    Result<std::uint32_t> type = find_memory_type(physical, requirements.memoryTypeBits,
                                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!type) {
        return type.error().with_context("composite image");
    }

    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = type.value();
    result = vkAllocateMemory(device, &allocate, nullptr, &memory);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot allocate composite image memory");
    }
    result = vkBindImageMemory(device, image, memory, 0);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot bind composite image memory");
    }

    VkImageViewCreateInfo view_create{};
    view_create.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_create.image = image;
    view_create.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_create.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_create.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    result = vkCreateImageView(device, &view_create, nullptr, &view);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot create a composite image view");
    }
    return ok();
}

[[nodiscard]] Result<void> create_host_buffer(VkDevice device, VkPhysicalDevice physical,
                                              VkDeviceSize size, VkBufferUsageFlags usage,
                                              VkBuffer& buffer, VkDeviceMemory& memory) {
    VkBufferCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create.size = size;
    create.usage = usage;
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(device, &create, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot create a host buffer");
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    // HOST_COHERENT avoids explicit flush and invalidate around the mapping.
    Result<std::uint32_t> type = find_memory_type(
        physical, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!type) {
        return type.error().with_context("host buffer");
    }

    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = type.value();
    result = vkAllocateMemory(device, &allocate, nullptr, &memory);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot allocate host buffer memory");
    }
    result = vkBindBufferMemory(device, buffer, memory, 0);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot bind host buffer memory");
    }
    return ok();
}

void image_barrier(VkCommandBuffer commands, VkImage image, VkImageLayout from, VkImageLayout to,
                   VkAccessFlags source_access, VkAccessFlags destination_access,
                   VkPipelineStageFlags source_stage, VkPipelineStageFlags destination_stage) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = source_access;
    barrier.dstAccessMask = destination_access;
    vkCmdPipelineBarrier(commands, source_stage, destination_stage, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);
}

}  // namespace rf::gpu::detail
