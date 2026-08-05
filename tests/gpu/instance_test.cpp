// GPU tests skip rather than fail when the machine has no Vulkan driver.
//
// A developer without a driver should still be able to run the suite, and a
// skip is visible in CTest output where a silently-passing test is not. On CI
// these run against Mesa's lavapipe -- a real Vulkan implementation in software,
// so API misuse and wrong results are caught even with no GPU present. See
// docs/adr/007-gpu-backend.md.

#include "rf/gpu/instance.hpp"

#include <gtest/gtest.h>

#include <string>

#include "rf/gpu/device_info.hpp"

namespace {

using rf::gpu::ApiVersion;
using rf::gpu::DeviceInfo;
using rf::gpu::DeviceKind;
using rf::gpu::Instance;
using rf::gpu::kMinimumApiVersion;

Instance make_instance() {
    Instance::Options options;
    options.enable_validation = true;
    auto instance = Instance::create(options);
    EXPECT_TRUE(instance.has_value())
        << (instance.has_error() ? instance.error().to_string() : "");
    return std::move(instance).value();
}

#define SKIP_WITHOUT_VULKAN()                                                    \
    do {                                                                         \
        if (!rf::gpu::vulkan_available()) {                                      \
            GTEST_SKIP() << "no Vulkan loader on this machine";                  \
        }                                                                        \
    } while (false)

// --- pure logic, no driver needed --------------------------------------------

TEST(DeviceInfo, VersionOrderingIsMajorThenMinorThenPatch) {
    DeviceInfo device;
    device.has_compute_queue = true;

    device.api_version = {1, 0, 0};
    EXPECT_FALSE(device.is_usable()) << "1.0 is below the 1.1 floor";

    device.api_version = kMinimumApiVersion;
    EXPECT_TRUE(device.is_usable());

    device.api_version = {2, 0, 0};
    EXPECT_TRUE(device.is_usable());

    device.api_version = {0, 9, 9};
    EXPECT_FALSE(device.is_usable()) << "a lower major must lose regardless of minor";
}

TEST(DeviceInfo, ADeviceWithoutComputeIsUnusable) {
    DeviceInfo device;
    device.api_version = {1, 3, 0};
    device.has_compute_queue = false;
    EXPECT_FALSE(device.is_usable());
    EXPECT_NE(device.unusable_reason().find("compute"), std::string::npos)
        << device.unusable_reason();
}

TEST(DeviceInfo, AnUnusableDeviceExplainsWhy) {
    // A user whose only GPU is unsuitable deserves to be told which one and
    // why, not shown an empty list.
    DeviceInfo old_driver;
    old_driver.has_compute_queue = true;
    old_driver.api_version = {1, 0, 0};
    const std::string reason = old_driver.unusable_reason();
    EXPECT_NE(reason.find("1.0.0"), std::string::npos) << reason;
    EXPECT_NE(reason.find(kMinimumApiVersion.to_string()), std::string::npos) << reason;
}

TEST(DeviceInfo, AUsableDeviceGivesNoReason) {
    DeviceInfo device;
    device.has_compute_queue = true;
    device.api_version = {1, 3, 0};
    ASSERT_TRUE(device.is_usable());
    EXPECT_TRUE(device.unusable_reason().empty());
}

TEST(DeviceInfo, KindsAllHaveNames) {
    for (const DeviceKind kind : {DeviceKind::discrete, DeviceKind::integrated,
                                  DeviceKind::virtualised, DeviceKind::software,
                                  DeviceKind::other}) {
        EXPECT_FALSE(to_string(kind).empty());
    }
}

TEST(ApiVersion, FormatsAsDottedTriple) {
    EXPECT_EQ((ApiVersion{1, 3, 250}.to_string()), "1.3.250");
}

// --- requires a driver -------------------------------------------------------

TEST(Instance, ReportsWhetherVulkanIsAvailableWithoutCrashing) {
    // Must be answerable on any machine, including one with no driver at all.
    const bool available = rf::gpu::vulkan_available();
    EXPECT_EQ(available, rf::gpu::vulkan_available()) << "the answer must be stable";
}

TEST(Instance, CreatesAndEnumerates) {
    SKIP_WITHOUT_VULKAN();
    const Instance instance = make_instance();

    const auto devices = instance.enumerate_devices();
    ASSERT_TRUE(devices.has_value()) << devices.error().to_string();
    for (const DeviceInfo& device : devices.value()) {
        EXPECT_FALSE(device.name.empty()) << "a device reported no name";
        EXPECT_GT(device.api_version.major, 0u) << device.name;
    }
}

TEST(Instance, EnumerationIsStable) {
    SKIP_WITHOUT_VULKAN();
    const Instance instance = make_instance();

    const auto first = instance.enumerate_devices();
    const auto second = instance.enumerate_devices();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_EQ(first.value().size(), second.value().size());
    for (std::size_t i = 0; i < first.value().size(); ++i) {
        EXPECT_EQ(first.value()[i].name, second.value()[i].name);
        EXPECT_EQ(first.value()[i].device_id, second.value()[i].device_id);
    }
}

TEST(Instance, PicksAUsableDeviceOrExplainsItself) {
    SKIP_WITHOUT_VULKAN();
    const Instance instance = make_instance();

    const auto devices = instance.enumerate_devices();
    ASSERT_TRUE(devices.has_value());
    if (devices.value().empty()) {
        GTEST_SKIP() << "the loader reported no devices";
    }

    const auto preferred = instance.preferred_device();
    if (preferred.has_value()) {
        ASSERT_LT(preferred.value(), devices.value().size());
        EXPECT_TRUE(devices.value()[preferred.value()].is_usable())
            << "chose a device it had already judged unusable";
    } else {
        // Refusing is fine; refusing without saying why is not.
        EXPECT_FALSE(preferred.error().message().empty());
    }
}

TEST(Instance, PrefersRealHardwareOverASoftwareImplementation) {
    // lavapipe is a correct Vulkan device and about two orders of magnitude too
    // slow to play back with, so it must never be chosen over a real GPU.
    SKIP_WITHOUT_VULKAN();
    const Instance instance = make_instance();

    const auto devices = instance.enumerate_devices();
    ASSERT_TRUE(devices.has_value());

    bool has_hardware = false;
    for (const DeviceInfo& device : devices.value()) {
        if (device.is_usable() && device.kind != DeviceKind::software) {
            has_hardware = true;
        }
    }
    if (!has_hardware) {
        GTEST_SKIP() << "no hardware device present; nothing to prefer over software";
    }

    const auto preferred = instance.preferred_device();
    ASSERT_TRUE(preferred.has_value()) << preferred.error().to_string();
    EXPECT_NE(devices.value()[preferred.value()].kind, DeviceKind::software)
        << "chose a software device while real hardware was available";
}

TEST(Instance, IsMovable) {
    SKIP_WITHOUT_VULKAN();
    Instance instance = make_instance();
    const Instance moved = std::move(instance);
    EXPECT_TRUE(moved.enumerate_devices().has_value())
        << "the instance did not survive being moved";
}

}  // namespace
