// Internal: the Vulkan handles behind Device.
//
// Included only by src/gpu/*.cpp.

#ifndef RF_GPU_DEVICE_IMPL_HPP
#define RF_GPU_DEVICE_IMPL_HPP

#include <memory>

#include <volk.h>

#include "instance_impl.hpp"
#include "rf/gpu/device.hpp"

namespace rf::gpu {

class Device::Impl {
public:
    /// Keeps the VkInstance alive for at least as long as this device.
    ///
    /// Declared first so it is destroyed LAST: member destruction runs in
    /// reverse declaration order, and the VkDevice below must be gone before
    /// the instance that owns its physical device. Getting this backwards
    /// crashed a test with 0xc0000409 before the ownership was made explicit.
    std::shared_ptr<Instance::Impl> instance;

    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    DeviceInfo info;

    /// True when VK_KHR_swapchain was enabled on this device. Checked before
    /// any swapchain call, because the alternative is dereferencing a null
    /// function pointer.
    bool can_present = false;

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    ~Impl() {
        if (device == VK_NULL_HANDLE) {
            return;
        }
        // Work may still be in flight if a submission failed part-way; waiting
        // makes destruction safe regardless of how we got here.
        vkDeviceWaitIdle(device);
        if (command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, command_pool, nullptr);
        }
        vkDestroyDevice(device, nullptr);
    }
};

}  // namespace rf::gpu

#endif  // RF_GPU_DEVICE_IMPL_HPP
