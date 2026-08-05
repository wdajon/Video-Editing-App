// Internal: the Vulkan handles behind Texture.

#ifndef RF_GPU_TEXTURE_IMPL_HPP
#define RF_GPU_TEXTURE_IMPL_HPP

#include <memory>
#include <string_view>

#include "rf/core/result.hpp"

#include <volk.h>

#include "device_impl.hpp"
#include "rf/gpu/texture.hpp"

namespace rf::gpu {

class Texture::Impl {
public:
    /// Declared first so it is destroyed last: the VkDevice below must outlive
    /// every handle here.
    std::shared_ptr<Device::Impl> device;

    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;

    /// Staging buffer, kept mapped for the texture's lifetime. A video layer
    /// uploads once per decoded frame, so mapping and unmapping per upload
    /// would be pure overhead on the render path.
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    void* staging_mapping = nullptr;

    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    int width = 0;
    int height = 0;

    /// False until the image has been given a defined layout. The first barrier
    /// must come from UNDEFINED; every later one from GENERAL.
    bool initialised = false;

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    /// Records into the texture's own command buffer.
    [[nodiscard]] Result<void> begin_commands();

    /// Submits that command buffer and waits. Uploads and readbacks are
    /// discrete operations with an observable result, so they synchronise; the
    /// playback path avoids calling them per frame rather than making them
    /// asynchronous.
    [[nodiscard]] Result<void> submit_and_wait(std::string_view what);

    ~Impl() {
        if (!device || device->device == VK_NULL_HANDLE) {
            return;
        }
        VkDevice handle = device->device;
        vkDeviceWaitIdle(handle);

        if (staging_mapping != nullptr) { vkUnmapMemory(handle, staging_memory); }
        if (fence != VK_NULL_HANDLE) { vkDestroyFence(handle, fence, nullptr); }
        if (commands != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(handle, device->command_pool, 1, &commands);
        }
        if (staging != VK_NULL_HANDLE) { vkDestroyBuffer(handle, staging, nullptr); }
        if (staging_memory != VK_NULL_HANDLE) { vkFreeMemory(handle, staging_memory, nullptr); }
        if (view != VK_NULL_HANDLE) { vkDestroyImageView(handle, view, nullptr); }
        if (image != VK_NULL_HANDLE) { vkDestroyImage(handle, image, nullptr); }
        if (memory != VK_NULL_HANDLE) { vkFreeMemory(handle, memory, nullptr); }
    }
};

}  // namespace rf::gpu

#endif  // RF_GPU_TEXTURE_IMPL_HPP
