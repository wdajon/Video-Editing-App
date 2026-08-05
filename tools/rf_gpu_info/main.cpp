// rf_gpu_info -- reports what Vulkan devices this machine offers and which one
// ReelForge would use.
//
// Exists because "the GPU layer works" is otherwise an untestable claim on a
// developer's machine, and because a user reporting a rendering problem needs a
// way to tell us what they are running without installing a Vulkan SDK.
//
// Exit codes: 0 a usable device was found, 1 none was, 2 no Vulkan at all.

#include <cstdio>

#include "rf/gpu/instance.hpp"

int main() {
    if (!rf::gpu::vulkan_available()) {
        std::fprintf(stderr,
                     "No Vulkan loader found. This machine has no Vulkan driver installed,\n"
                     "or the driver is broken. ReelForge cannot use the GPU here.\n");
        return 2;
    }

    rf::gpu::Instance::Options options;
    options.enable_validation = true;

    auto instance = rf::gpu::Instance::create(options);
    if (!instance) {
        std::fprintf(stderr, "%s\n", instance.error().to_string().c_str());
        return 2;
    }

    std::printf("validation layers: %s\n",
                instance.value().validation_enabled() ? "enabled" : "unavailable");

    auto devices = instance.value().enumerate_devices();
    if (!devices) {
        std::fprintf(stderr, "%s\n", devices.error().to_string().c_str());
        return 2;
    }

    if (devices.value().empty()) {
        std::fprintf(stderr, "The Vulkan loader reported no devices.\n");
        return 1;
    }

    const auto preferred = instance.value().preferred_device();
    const std::size_t chosen =
        preferred.has_value() ? preferred.value() : devices.value().size();

    std::printf("\n%zu device(s):\n", devices.value().size());
    for (std::size_t i = 0; i < devices.value().size(); ++i) {
        const rf::gpu::DeviceInfo& device = devices.value()[i];
        std::printf("\n  [%zu]%s %s\n", i, i == chosen ? " *" : "  ", device.name.c_str());
        std::printf("        kind:    %.*s\n", static_cast<int>(to_string(device.kind).size()),
                    to_string(device.kind).data());
        std::printf("        vulkan:  %s\n", device.api_version.to_string().c_str());
        std::printf("        memory:  %.1f GiB device-local\n",
                    static_cast<double>(device.device_local_memory) / (1024.0 * 1024.0 * 1024.0));
        if (device.has_compute_queue) {
            std::printf("        compute: queue family %u\n", device.compute_queue_family);
        } else {
            std::printf("        compute: none\n");
        }
        if (!device.is_usable()) {
            std::printf("        UNUSABLE: %s\n", device.unusable_reason().c_str());
        }
    }

    if (!preferred) {
        std::printf("\nNo usable device: %s\n", preferred.error().message().c_str());
        return 1;
    }
    std::printf("\nReelForge would use [%zu] %s\n", chosen,
                devices.value()[chosen].name.c_str());
    return 0;
}
