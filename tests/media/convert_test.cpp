#include "rf/media/convert.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "rf/media/decoder.hpp"
#include "rf/media/video_frame.hpp"

namespace {

using rf::media::VideoDecoder;
using rf::media::VideoFrame;
using rf::media::to_rgba8;

std::filesystem::path fixture(const std::string& name) {
    return std::filesystem::path{RF_TEST_FIXTURE_DIR} / "media" / name;
}

VideoFrame first_frame(const std::string& name) {
    auto decoder = VideoDecoder::open(fixture(name));
    EXPECT_TRUE(decoder.has_value()) << (decoder.has_error() ? decoder.error().to_string() : "");
    if (!decoder) {
        return {};
    }
    auto frame = decoder.value().next_frame();
    EXPECT_TRUE(frame.has_value()) << (frame.has_error() ? frame.error().to_string() : "");
    if (!frame || !frame.value().has_value()) {
        return {};
    }
    return std::move(frame).value().value();
}

TEST(ToRgba8, RejectsAnEmptyFrame) {
    const VideoFrame empty;
    EXPECT_TRUE(to_rgba8(empty).has_error());
}

TEST(ToRgba8, ConvertsADecodedFrame) {
    const VideoFrame source = first_frame("bars_320x240_30fps_h264_aac.mp4");
    ASSERT_FALSE(source.is_empty());
    ASSERT_EQ(source.pixel_format(), "yuv420p");

    const auto converted = to_rgba8(source);
    ASSERT_TRUE(converted.has_value()) << converted.error().to_string();
    EXPECT_EQ(converted.value().pixel_format(), "rgba");
    EXPECT_EQ(converted.value().width(), 320);
    EXPECT_EQ(converted.value().height(), 240);
    EXPECT_EQ(converted.value().pixels().size(), 320u * 240u * 4u);
}

TEST(ToRgba8, PreservesTimingMetadata) {
    // A converted frame that lost its timestamp would be unschedulable, and the
    // loss would only show up as drift much later.
    const VideoFrame source = first_frame("bars_320x240_2997fps_h264.mp4");
    ASSERT_FALSE(source.is_empty());

    const auto converted = to_rgba8(source);
    ASSERT_TRUE(converted.has_value()) << converted.error().to_string();
    EXPECT_EQ(converted.value().presentation_timestamp, source.presentation_timestamp);
    EXPECT_EQ(converted.value().time_base, source.time_base);
    EXPECT_EQ(converted.value().frame_index, source.frame_index);
}

TEST(ToRgba8, ProducesOpaquePixels) {
    // The source has no alpha channel, so every pixel must come out opaque
    // rather than transparent -- a frame that composited to nothing would look
    // like a decode failure.
    const VideoFrame source = first_frame("bars_320x240_30fps_h264_aac.mp4");
    ASSERT_FALSE(source.is_empty());

    const auto converted = to_rgba8(source);
    ASSERT_TRUE(converted.has_value()) << converted.error().to_string();
    for (std::size_t i = 3; i < converted.value().pixels().size(); i += 4) {
        ASSERT_EQ(converted.value().pixels()[i], 255u) << "alpha at byte " << i;
    }
}

TEST(ToRgba8, ProducesSomethingOtherThanAFlatColour) {
    // testsrc2 is colour bars. If conversion silently produced a uniform buffer
    // -- a wrong stride, an unfilled plane -- every downstream comparison would
    // still "work" and show nothing.
    const VideoFrame source = first_frame("bars_320x240_30fps_h264_aac.mp4");
    ASSERT_FALSE(source.is_empty());

    const auto converted = to_rgba8(source);
    ASSERT_TRUE(converted.has_value()) << converted.error().to_string();

    const auto& pixels = converted.value().pixels();
    bool varies = false;
    for (std::size_t i = 4; i < pixels.size(); i += 4) {
        if (pixels[i] != pixels[0] || pixels[i + 1] != pixels[1] || pixels[i + 2] != pixels[2]) {
            varies = true;
            break;
        }
    }
    EXPECT_TRUE(varies) << "the converted frame is a single flat colour";
}

TEST(ToRgba8, IsIdempotentOnAnAlreadyConvertedFrame) {
    const VideoFrame source = first_frame("bars_320x240_30fps_h264_aac.mp4");
    ASSERT_FALSE(source.is_empty());

    const auto once = to_rgba8(source);
    ASSERT_TRUE(once.has_value());
    const auto twice = to_rgba8(once.value());
    ASSERT_TRUE(twice.has_value());
    EXPECT_EQ(twice.value().pixels(), once.value().pixels());
}

TEST(ToRgba8, IsDeterministic) {
    const VideoFrame source = first_frame("bars_320x240_2997fps_h264.mp4");
    ASSERT_FALSE(source.is_empty());
    const auto first = to_rgba8(source);
    const auto second = to_rgba8(source);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first.value().pixels(), second.value().pixels());
}

}  // namespace
