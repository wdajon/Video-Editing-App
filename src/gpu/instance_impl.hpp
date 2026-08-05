// Internal: the Vulkan handles behind Instance.
//
// Included only by src/gpu/*.cpp. It includes volk, so nothing outside this
// module may include it -- ADR 007's layering rule, which the PRIVATE include
// directories in src/gpu/CMakeLists.txt enforce.

#ifndef RF_GPU_INSTANCE_IMPL_HPP
#define RF_GPU_INSTANCE_IMPL_HPP

#include <volk.h>

#include "rf/gpu/instance.hpp"

namespace rf::gpu {

class Instance::Impl {
public:
    VkInstance instance = VK_NULL_HANDLE;
    bool validation = false;

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    ~Impl() {
        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
        }
    }
};

}  // namespace rf::gpu

#endif  // RF_GPU_INSTANCE_IMPL_HPP
