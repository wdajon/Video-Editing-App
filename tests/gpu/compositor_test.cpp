#include "rf/gpu/compositor.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "rf/gpu/device.hpp"
#include "rf/gpu/image.hpp"
#include "rf/gpu/instance.hpp"

namespace {

using rf::gpu::Compositor;
using rf::gpu::Device;
using rf::gpu::ImageRgba8;
using rf::gpu::Instance;
using rf::gpu::Layer;
using rf::gpu::composite_reference;

struct Harness {
    Instance instance;
    Device device;
    Compositor compositor;
};

/// Opens a device and compositor, or returns nothing so the caller can skip.
std::optional<Harness> open_harness() {
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
    auto compositor = Compositor::create(device.value());
    if (!compositor) {
        return std::nullopt;
    }
    return Harness{std::move(instance).value(), std::move(device).value(),
                   std::move(compositor).value()};
}

ImageRgba8 solid(int width, int height, std::uint8_t red, std::uint8_t green, std::uint8_t blue,
                 std::uint8_t alpha) {
    ImageRgba8 image;
    image.width = width;
    image.height = height;
    image.pixels.resize(image.expected_size());
    for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
        image.pixels[i + 0] = red;
        image.pixels[i + 1] = green;
        image.pixels[i + 2] = blue;
        image.pixels[i + 3] = alpha;
    }
    return image;
}

/// Compares within `tolerance` per channel, naming the first pixel that fails.
::testing::AssertionResult images_match(const ImageRgba8& actual, const ImageRgba8& expected,
                                        int tolerance) {
    if (actual.width != expected.width || actual.height != expected.height) {
        return ::testing::AssertionFailure()
               << "size mismatch: " << actual.width << "x" << actual.height << " vs "
               << expected.width << "x" << expected.height;
    }
    for (std::size_t i = 0; i < expected.pixels.size(); ++i) {
        const int difference =
            static_cast<int>(actual.pixels[i]) - static_cast<int>(expected.pixels[i]);
        if (difference > tolerance || difference < -tolerance) {
            const std::size_t pixel = i / 4;
            return ::testing::AssertionFailure()
                   << "pixel (" << (pixel % static_cast<std::size_t>(actual.width)) << ", "
                   << (pixel / static_cast<std::size_t>(actual.width)) << ") channel " << (i % 4)
                   << ": got " << static_cast<int>(actual.pixels[i]) << ", expected "
                   << static_cast<int>(expected.pixels[i]) << " (tolerance " << tolerance << ")";
        }
    }
    return ::testing::AssertionSuccess();
}

// --- CPU reference, no device needed -----------------------------------------

TEST(CompositeReference, NoLayersIsOpaqueBlack) {
    const ImageRgba8 result = composite_reference({}, 4, 4);
    ASSERT_TRUE(result.is_valid());
    for (std::size_t i = 0; i < result.pixels.size(); i += 4) {
        ASSERT_EQ(result.pixels[i + 0], 0);
        ASSERT_EQ(result.pixels[i + 1], 0);
        ASSERT_EQ(result.pixels[i + 2], 0);
        ASSERT_EQ(result.pixels[i + 3], 255) << "the program monitor frame must be opaque";
    }
}

TEST(CompositeReference, OrderMatters) {
    // If stacking order did not change the result, the GPU comparison could
    // pass with the layers composited in any order at all.
    const std::vector<Layer> red_over_blue = {{solid(2, 2, 0, 0, 255, 255), 1.0F, true},
                                              {solid(2, 2, 255, 0, 0, 255), 1.0F, true}};
    const std::vector<Layer> blue_over_red = {{solid(2, 2, 255, 0, 0, 255), 1.0F, true},
                                              {solid(2, 2, 0, 0, 255, 255), 1.0F, true}};
    EXPECT_NE(composite_reference(red_over_blue, 2, 2), composite_reference(blue_over_red, 2, 2));
}

// --- the GPU path -------------------------------------------------------------

TEST(Compositor, NoLayersProducesOpaqueBlack) {
    auto harness = open_harness();
    if (!harness.has_value()) {
        GTEST_SKIP() << "no usable Vulkan device";
    }
    auto result = harness->compositor.composite({}, 32, 32);
    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_TRUE(images_match(result.value(), composite_reference({}, 32, 32), 0));
}

TEST(Compositor, ADisabledLayerIsNotDrawn) {
    auto harness = open_harness();
    if (!harness.has_value()) {
        GTEST_SKIP() << "no usable Vulkan device";
    }
    const std::vector<Layer> layers = {{solid(16, 16, 255, 0, 0, 255), 1.0F, false}};
    auto result = harness->compositor.composite(layers, 16, 16);
    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_TRUE(images_match(result.value(), composite_reference({}, 16, 16), 0));
}

TEST(Compositor, AnOpaqueLayerReplacesTheBackdropExactly) {
    // Coverage is exactly 1, so the blend is an identity on the source and the
    // comparison can be exact rather than tolerant.
    auto harness = open_harness();
    if (!harness.has_value()) {
        GTEST_SKIP() << "no usable Vulkan device";
    }
    const std::vector<Layer> layers = {{solid(16, 16, 12, 200, 77, 255), 1.0F, true}};
    auto result = harness->compositor.composite(layers, 16, 16);
    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_TRUE(images_match(result.value(), composite_reference(layers, 16, 16), 0));
}

TEST(Compositor, AFullyTransparentLayerChangesNothing) {
    auto harness = open_harness();
    if (!harness.has_value()) {
        GTEST_SKIP() << "no usable Vulkan device";
    }
    const std::vector<Layer> layers = {{solid(16, 16, 255, 255, 255, 0), 1.0F, true}};
    auto result = harness->compositor.composite(layers, 16, 16);
    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_TRUE(images_match(result.value(), composite_reference({}, 16, 16), 0));
}

TEST(Compositor, ZeroOpacityHidesAnOpaqueLayer) {
    auto harness = open_harness();
    if (!harness.has_value()) {
        GTEST_SKIP() << "no usable Vulkan device";
    }
    const std::vector<Layer> layers = {{solid(16, 16, 255, 0, 0, 255), 0.0F, true}};
    auto result = harness->compositor.composite(layers, 16, 16);
    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_TRUE(images_match(result.value(), composite_reference({}, 16, 16), 0));
}

TEST(Compositor, TheTopLayerWins) {
    auto harness = open_harness();
    if (!harness.has_value()) {
        GTEST_SKIP() << "no usable Vulkan device";
    }
    const std::vector<Layer> layers = {{solid(16, 16, 0, 0, 255, 255), 1.0F, true},
                                       {solid(16, 16, 255, 0, 0, 255), 1.0F, true}};
    auto result = harness->compositor.composite(layers, 16, 16);
    ASSERT_TRUE(result.has_value()) << result.error().to_string();

    // Red is last in the list, so it is on top.
    EXPECT_EQ(result.value().pixels[0], 255) << "red";
    EXPECT_EQ(result.value().pixels[2], 0) << "blue should be covered";
    EXPECT_TRUE(images_match(result.value(), composite_reference(layers, 16, 16), 0));
}

TEST(Compositor, StackingOrderIsRespected) {
    auto harness = open_harness();
    if (!harness.has_value()) {
        GTEST_SKIP() << "no usable Vulkan device";
    }
    const std::vector<Layer> red_over_blue = {{solid(8, 8, 0, 0, 255, 255), 1.0F, true},
                                              {solid(8, 8, 255, 0, 0, 255), 1.0F, true}};
    const std::vector<Layer> blue_over_red = {{solid(8, 8, 255, 0, 0, 255), 1.0F, true},
                                              {solid(8, 8, 0, 0, 255, 255), 1.0F, true}};

    auto first = harness->compositor.composite(red_over_blue, 8, 8);
    auto second = harness->compositor.composite(blue_over_red, 8, 8);
    ASSERT_TRUE(first.has_value()) << first.error().to_string();
    ASSERT_TRUE(second.has_value()) << second.error().to_string();
    EXPECT_NE(first.value(), second.value()) << "layer order had no effect";
}

TEST(Compositor, BlendsPartialAlpha) {
    // Half-transparent white over black. Rounding between the GPU's float path
    // and the CPU reference can differ in the last bit, so this allows +/-1 per
    // channel -- a far tighter claim than an SSIM threshold.
    auto harness = open_harness();
    if (!harness.has_value()) {
        GTEST_SKIP() << "no usable Vulkan device";
    }
    const std::vector<Layer> layers = {{solid(16, 16, 255, 255, 255, 128), 1.0F, true}};
    auto result = harness->compositor.composite(layers, 16, 16);
    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_TRUE(images_match(result.value(), composite_reference(layers, 16, 16), 1));

    // Sanity: it really is a mid grey, not one of the extremes.
    EXPECT_GT(result.value().pixels[0], 100);
    EXPECT_LT(result.value().pixels[0], 155);
}

TEST(Compositor, AppliesOpacityOnTopOfAlpha) {
    auto harness = open_harness();
    if (!harness.has_value()) {
        GTEST_SKIP() << "no usable Vulkan device";
    }
    const std::vector<Layer> layers = {{solid(16, 16, 255, 255, 255, 255), 0.25F, true}};
    auto result = harness->compositor.composite(layers, 16, 16);
    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_TRUE(images_match(result.value(), composite_reference(layers, 16, 16), 1));
    EXPECT_NEAR(result.value().pixels[0], 64, 2);
}

TEST(Compositor, CompositesThreeLayersAtTheGateResolution) {
    // The shape M3's gate is stated in: three layers at 1080x1920. This checks
    // the result is right. It says nothing about frame rate, which is measured
    // on the reference machine and recorded in docs/PROGRESS.md.
    auto harness = open_harness();
    if (!harness.has_value()) {
        GTEST_SKIP() << "no usable Vulkan device";
    }
    const std::vector<Layer> layers = {
        {solid(1080, 1920, 20, 40, 60, 255), 1.0F, true},
        {solid(1080, 1920, 200, 30, 30, 128), 1.0F, true},
        {solid(1080, 1920, 255, 255, 255, 64), 0.5F, true},
    };
    auto result = harness->compositor.composite(layers, 1080, 1920);
    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_TRUE(images_match(result.value(), composite_reference(layers, 1080, 1920), 1));
}

TEST(Compositor, RepeatedCompositesAreIdentical) {
    auto harness = open_harness();
    if (!harness.has_value()) {
        GTEST_SKIP() << "no usable Vulkan device";
    }
    const std::vector<Layer> layers = {{solid(32, 32, 10, 20, 30, 255), 1.0F, true},
                                       {solid(32, 32, 90, 80, 70, 100), 0.75F, true}};
    auto first = harness->compositor.composite(layers, 32, 32);
    ASSERT_TRUE(first.has_value()) << first.error().to_string();
    for (int i = 0; i < 3; ++i) {
        auto again = harness->compositor.composite(layers, 32, 32);
        ASSERT_TRUE(again.has_value()) << again.error().to_string();
        ASSERT_EQ(again.value(), first.value()) << "composite " << i << " differed";
    }
}

TEST(Compositor, RejectsALayerOfTheWrongSize) {
    // Silently stretching would hide a caller's mistake; transforms belong with
    // the keyframe system, not here.
    auto harness = open_harness();
    if (!harness.has_value()) {
        GTEST_SKIP() << "no usable Vulkan device";
    }
    const std::vector<Layer> layers = {{solid(8, 8, 255, 0, 0, 255), 1.0F, true}};
    auto result = harness->compositor.composite(layers, 16, 16);
    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(result.error().code(), rf::Errc::invalid_argument);
    EXPECT_NE(result.error().message().find("8x8"), std::string::npos)
        << result.error().message();
}

TEST(Compositor, RejectsNonPositiveDimensions) {
    auto harness = open_harness();
    if (!harness.has_value()) {
        GTEST_SKIP() << "no usable Vulkan device";
    }
    EXPECT_TRUE(harness->compositor.composite({}, 0, 16).has_error());
    EXPECT_TRUE(harness->compositor.composite({}, 16, -4).has_error());
}

TEST(Compositor, HandlesASizeThatIsNotAWorkgroupMultiple) {
    auto harness = open_harness();
    if (!harness.has_value()) {
        GTEST_SKIP() << "no usable Vulkan device";
    }
    const std::vector<Layer> layers = {{solid(53, 29, 77, 88, 99, 255), 1.0F, true}};
    auto result = harness->compositor.composite(layers, 53, 29);
    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_TRUE(images_match(result.value(), composite_reference(layers, 53, 29), 0));
}

}  // namespace
