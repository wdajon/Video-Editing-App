#include "rf/gpu/instance.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <volk.h>

namespace rf::gpu {
namespace {

/// volk must initialise once per process before any Vulkan call. It returns a
/// failure rather than aborting when no loader is present, which is the case on
/// a machine with no driver -- an ordinary situation, not a fatal one.
bool initialise_volk() {
    static const bool ok = [] { return volkInitialize() == VK_SUCCESS; }();
    return ok;
}

[[nodiscard]] Error from_vulkan(VkResult result, std::string_view context) {
    Errc code = Errc::internal;
    switch (result) {
        case VK_ERROR_OUT_OF_HOST_MEMORY:
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            code = Errc::out_of_memory;
            break;
        case VK_ERROR_INITIALIZATION_FAILED:
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            code = Errc::unsupported_format;
            break;
        case VK_ERROR_DEVICE_LOST:
            code = Errc::device_lost;
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

[[nodiscard]] DeviceKind classify(VkPhysicalDeviceType type) noexcept {
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return DeviceKind::discrete;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return DeviceKind::integrated;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return DeviceKind::virtualised;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            return DeviceKind::software;
        case VK_PHYSICAL_DEVICE_TYPE_OTHER:
        default:                                     return DeviceKind::other;
    }
}

[[nodiscard]] bool layer_available(const char* wanted) {
    std::uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS || count == 0) {
        return false;
    }
    std::vector<VkLayerProperties> layers(count);
    if (vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS) {
        return false;
    }
    return std::any_of(layers.begin(), layers.end(), [wanted](const VkLayerProperties& layer) {
        return std::strcmp(layer.layerName, wanted) == 0;
    });
}

/// Ranks device kinds for automatic selection. Software last: it is a correct
/// Vulkan implementation and about two orders of magnitude too slow to play
/// back with, so it must never be chosen over real hardware.
[[nodiscard]] int preference_rank(DeviceKind kind) noexcept {
    switch (kind) {
        case DeviceKind::discrete:    return 0;
        case DeviceKind::integrated:  return 1;
        case DeviceKind::virtualised: return 2;
        case DeviceKind::other:       return 3;
        case DeviceKind::software:    return 4;
    }
    return 5;
}

}  // namespace

class Instance::Impl {
public:
    VkInstance instance = VK_NULL_HANDLE;
    bool validation = false;

    ~Impl() {
        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
        }
    }

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
};

bool vulkan_available() noexcept {
    return initialise_volk();
}

Instance::Instance(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Instance::Instance(Instance&&) noexcept = default;
Instance& Instance::operator=(Instance&&) noexcept = default;
Instance::~Instance() = default;

Result<Instance> Instance::create(const Options& options) {
    if (!initialise_volk()) {
        return Error{Errc::not_found,
                     "no Vulkan loader found; the machine has no Vulkan driver installed"};
    }

    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = options.application_name.data();
    application.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    application.pEngineName = "ReelForge";
    application.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    application.apiVersion =
        VK_MAKE_API_VERSION(0, kMinimumApiVersion.major, kMinimumApiVersion.minor, 0);

    std::vector<const char*> layers;
    static constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
    const bool validation = options.enable_validation && layer_available(kValidationLayer);
    if (validation) {
        layers.push_back(kValidationLayer);
    }

    VkInstanceCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create.pApplicationInfo = &application;
    create.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    create.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

    auto impl = std::make_unique<Impl>();
    const VkResult result = vkCreateInstance(&create, nullptr, &impl->instance);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot create a Vulkan instance");
    }
    impl->validation = validation;

    // Loading instance-level entry points here means later calls go straight to
    // the driver rather than through the loader's dispatch trampoline.
    volkLoadInstanceOnly(impl->instance);

    return Instance{std::move(impl)};
}

bool Instance::validation_enabled() const noexcept {
    return impl_->validation;
}

Result<std::vector<DeviceInfo>> Instance::enumerate_devices() const {
    std::uint32_t count = 0;
    VkResult result = vkEnumeratePhysicalDevices(impl_->instance, &count, nullptr);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot enumerate physical devices");
    }
    if (count == 0) {
        return std::vector<DeviceInfo>{};
    }

    std::vector<VkPhysicalDevice> handles(count);
    result = vkEnumeratePhysicalDevices(impl_->instance, &count, handles.data());
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot enumerate physical devices");
    }

    std::vector<DeviceInfo> devices;
    devices.reserve(handles.size());

    for (const VkPhysicalDevice handle : handles) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(handle, &properties);

        DeviceInfo info;
        info.name = properties.deviceName;
        info.kind = classify(properties.deviceType);
        info.api_version = {VK_API_VERSION_MAJOR(properties.apiVersion),
                            VK_API_VERSION_MINOR(properties.apiVersion),
                            VK_API_VERSION_PATCH(properties.apiVersion)};
        info.vendor_id = properties.vendorID;
        info.device_id = properties.deviceID;

        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(handle, &memory);
        for (std::uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
            if ((memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
                info.device_local_memory += memory.memoryHeaps[i].size;
            }
        }

        std::uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(handle, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(handle, &family_count, families.data());

        // Compute, not graphics: compositing is done with compute shaders, and
        // requiring graphics would rule out devices that could do the work.
        for (std::uint32_t i = 0; i < family_count; ++i) {
            if ((families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0 &&
                families[i].queueCount > 0) {
                info.has_compute_queue = true;
                info.compute_queue_family = i;
                break;
            }
        }

        devices.push_back(std::move(info));
    }

    return devices;
}

Result<std::size_t> Instance::preferred_device() const {
    Result<std::vector<DeviceInfo>> devices = enumerate_devices();
    if (!devices) {
        return devices.error();
    }
    const std::vector<DeviceInfo>& list = devices.value();

    std::size_t best = list.size();
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (!list[i].is_usable()) {
            continue;
        }
        if (best == list.size()) {
            best = i;
            continue;
        }
        const int rank = preference_rank(list[i].kind);
        const int best_rank = preference_rank(list[best].kind);
        if (rank < best_rank ||
            (rank == best_rank && list[i].device_local_memory > list[best].device_local_memory)) {
            best = i;
        }
    }

    if (best == list.size()) {
        if (list.empty()) {
            return Error{Errc::not_found, "the Vulkan loader reported no devices"};
        }
        return Error{Errc::unsupported_format,
                     "no usable GPU: " + list.front().name + " " + list.front().unusable_reason()};
    }
    return best;
}

}  // namespace rf::gpu
