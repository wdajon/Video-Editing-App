#include "rf/gpu/device.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "rf/gpu/image.hpp"
#include "rf/gpu/instance.hpp"

namespace {

using rf::gpu::Device;
using rf::gpu::ImageRgba8;
using rf::gpu::Instance;
using rf::gpu::expected_fill_pattern;

#define SKIP_WITHOUT_DEVICE(device_variable)                                      \
    do {                                                                          \
        if (!rf::gpu::vulkan_available()) {                                       \
            GTEST_SKIP() << "no Vulkan loader on this machine";                   \
        }                                                                         \
    } while (false)

/// Opens the preferred device, or skips. Returns nullopt when there is none.
std::optional<Device> open_device() {
    if (!rf::gpu::vulkan_available()) {
        return std::nullopt;
    }
    Instance::Options options;
    options.enable_validation = true;
    auto instance = Instance::create(options);
    if (!instance) {
        return std::nullopt;
    }
    auto device = Device::create_preferred(instance.value());
    if (!device) {
        return std::nullopt;
    }
    return std::move(device).value();
}

/// Reports the first differing pixel rather than just "not equal". A whole-image
/// comparison that fails with no location is nearly useless for diagnosis.
::testing::AssertionResult images_match(const ImageRgba8& actual, const ImageRgba8& expected) {
    if (actual.width != expected.width || actual.height != expected.height) {
        return ::testing::AssertionFailure()
               << "size mismatch: got " << actual.width << "x" << actual.height << ", expected "
               << expected.width << "x" << expected.height;
    }
    if (actual.pixels.size() != expected.pixels.size()) {
        return ::testing::AssertionFailure()
               << "byte count mismatch: got " << actual.pixels.size() << ", expected "
               << expected.pixels.size();
    }
    for (std::size_t i = 0; i < expected.pixels.size(); ++i) {
        if (actual.pixels[i] != expected.pixels[i]) {
            const std::size_t pixel = i / 4;
            return ::testing::AssertionFailure()
                   << "first difference at pixel (" << (pixel % static_cast<std::size_t>(actual.width))
                   << ", " << (pixel / static_cast<std::size_t>(actual.width)) << ") channel "
                   << (i % 4) << ": got " << static_cast<int>(actual.pixels[i]) << ", expected "
                   << static_cast<int>(expected.pixels[i]);
        }
    }
    return ::testing::AssertionSuccess();
}

// --- CPU reference, no device needed -----------------------------------------

TEST(ExpectedFillPattern, HasTheRightShape) {
    const ImageRgba8 image = expected_fill_pattern(16, 9);
    EXPECT_TRUE(image.is_valid());
    EXPECT_EQ(image.pixels.size(), 16u * 9u * 4u);
}

TEST(ExpectedFillPattern, EncodesXAndYDifferently) {
    // If the two axes were encoded the same way, a transposed image would still
    // compare equal and the GPU test would pass while being wrong.
    const ImageRgba8 image = expected_fill_pattern(4, 4);
    const auto at = [&image](int x, int y, int channel) {
        return image.pixels[(static_cast<std::size_t>(y) * 4u + static_cast<std::size_t>(x)) * 4u +
                            static_cast<std::size_t>(channel)];
    };
    EXPECT_EQ(at(3, 0, 0), 3) << "red should carry x";
    EXPECT_EQ(at(3, 0, 1), 0) << "green should carry y";
    EXPECT_NE(at(3, 0, 0), at(0, 3, 0)) << "the pattern is symmetric under transposition";
}

TEST(ExpectedFillPattern, IsOpaque) {
    const ImageRgba8 image = expected_fill_pattern(8, 8);
    for (std::size_t i = 3; i < image.pixels.size(); i += 4) {
        ASSERT_EQ(image.pixels[i], 255u) << "alpha at byte " << i;
    }
}

TEST(ExpectedFillPattern, RejectsNonPositiveSizes) {
    EXPECT_FALSE(expected_fill_pattern(0, 8).is_valid());
    EXPECT_FALSE(expected_fill_pattern(8, -1).is_valid());
}

// --- the GPU path -------------------------------------------------------------

TEST(Device, OpensThePreferredDevice) {
    SKIP_WITHOUT_DEVICE(device);
    auto device = open_device();
    if (!device.has_value()) {
        GTEST_SKIP() << "no usable Vulkan device";
    }
    EXPECT_FALSE(device->info().name.empty());
    EXPECT_TRUE(device->info().is_usable());
}

TEST(Device, RejectsAnOutOfRangeDeviceIndex) {
    SKIP_WITHOUT_DEVICE(device);
    Instance::Options options;
    auto instance = Instance::create(options);
    if (!instance) {
        GTEST_SKIP() << "cannot create a Vulkan instance";
    }
    const auto device = Device::create(instance.value(), 9999);
    ASSERT_TRUE(device.has_error());
    EXPECT_EQ(device.error().code(), rf::Errc::not_found);
}

TEST(Device, RendersThePatternExactly) {
    // The whole compute path end to end: device, memory, storage image,
    // descriptors, pipeline, dispatch, barriers, submission, readback -- checked
    // against a result predicted on the CPU rather than captured from a GPU.
    auto device = open_device();
    if (!device.has_value()) {
        GTEST_SKIP() << "no usable Vulkan device";
    }

    auto rendered = device->render_fill_pattern(64, 48);
    ASSERT_TRUE(rendered.has_value()) << rendered.error().to_string();
    EXPECT_TRUE(images_match(rendered.value(), expected_fill_pattern(64, 48)));
}

TEST(Device, WritesEveryPixelWhenTheSizeIsNotAWorkgroupMultiple) {
    // 61x37 against an 8x8 workgroup: the dispatch is rounded up, so the shader
    // must bounds-check, and the edges must still be written rather than left
    // as whatever the allocation happened to contain.
    auto device = open_device();
    if (!device.has_value()) {
        GTEST_SKIP() << "no usable Vulkan device";
    }

    auto rendered = device->render_fill_pattern(61, 37);
    ASSERT_TRUE(rendered.has_value()) << rendered.error().to_string();
    EXPECT_TRUE(images_match(rendered.value(), expected_fill_pattern(61, 37)));
}

TEST(Device, RendersASinglePixel) {
    auto device = open_device();
    if (!device.has_value()) {
        GTEST_SKIP() << "no usable Vulkan device";
    }
    auto rendered = device->render_fill_pattern(1, 1);
    ASSERT_TRUE(rendered.has_value()) << rendered.error().to_string();
    EXPECT_TRUE(images_match(rendered.value(), expected_fill_pattern(1, 1)));
}

TEST(Device, RendersAVerticalReelsFrame) {
    // 1080x1920 is the shape M3's gate is stated in. Correctness here says
    // nothing about frame rate, which is measured on the reference machine.
    auto device = open_device();
    if (!device.has_value()) {
        GTEST_SKIP() << "no usable Vulkan device";
    }
    auto rendered = device->render_fill_pattern(1080, 1920);
    ASSERT_TRUE(rendered.has_value()) << rendered.error().to_string();
    EXPECT_TRUE(images_match(rendered.value(), expected_fill_pattern(1080, 1920)));
}

TEST(Device, RepeatedRendersAreIdentical) {
    // Catches resources leaking between dispatches and uninitialised state that
    // happens to be right the first time.
    auto device = open_device();
    if (!device.has_value()) {
        GTEST_SKIP() << "no usable Vulkan device";
    }
    auto first = device->render_fill_pattern(32, 32);
    ASSERT_TRUE(first.has_value()) << first.error().to_string();
    for (int i = 0; i < 4; ++i) {
        auto again = device->render_fill_pattern(32, 32);
        ASSERT_TRUE(again.has_value()) << again.error().to_string();
        ASSERT_EQ(again.value(), first.value()) << "render " << i << " differed";
    }
}

TEST(Device, RejectsNonPositiveDimensions) {
    auto device = open_device();
    if (!device.has_value()) {
        GTEST_SKIP() << "no usable Vulkan device";
    }
    EXPECT_TRUE(device->render_fill_pattern(0, 16).has_error());
    EXPECT_TRUE(device->render_fill_pattern(16, -1).has_error());
}

}  // namespace
