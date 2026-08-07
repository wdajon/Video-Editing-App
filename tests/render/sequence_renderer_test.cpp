// The first path from a document to a picture, end to end: model, decoder,
// converter, compositor.
//
// These need a Vulkan device. On CI's Linux runners that is Mesa's lavapipe, a
// real software implementation -- so wrong pixels and API misuse are caught, and
// nothing here says anything about speed (ADR 007).

#include "rf/render/sequence_renderer.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "rf/timeline/composition.hpp"

namespace {

using rf::Errc;
using rf::gpu::Device;
using rf::gpu::ImageRgba8;
using rf::gpu::Instance;
using rf::media::Rational;
using rf::render::SequenceRenderer;
using rf::timeline::Document;
using rf::timeline::Ticks;
using rf::timeline::TrackId;
using rf::timeline::TrackKind;

constexpr Ticks kFrame = 3000;  // 30 fps at a 1/90000 base
constexpr int kWidth = 320;
constexpr int kHeight = 240;

std::filesystem::path fixture() {
    return std::filesystem::path(RF_TEST_FIXTURE_DIR) / "media" /
           "bars_320x240_30fps_h264_aac.mp4";
}

/// A device, or nullopt when the machine cannot provide one. Skipping is honest
/// here: a developer without Vulkan should see the rest of the suite pass rather
/// than a failure that says nothing about their change.
class RendererTest : public ::testing::Test {
protected:
    void SetUp() override {
        // No presentation: rendering to pixels needs no surface, and asking for
        // one would fail on a machine that cannot present while rendering there
        // is perfectly possible (ADR 008).
        auto instance = Instance::create(Instance::Options{});
        if (!instance) {
            GTEST_SKIP() << "no Vulkan instance: " << instance.error().to_string();
        }
        instance_ = std::make_unique<Instance>(std::move(instance).value());

        auto device = Device::create_preferred(*instance_);
        if (!device) {
            GTEST_SKIP() << "no Vulkan device: " << device.error().to_string();
        }
        device_ = std::make_unique<Device>(std::move(device).value());
    }

    [[nodiscard]] SequenceRenderer make_renderer() {
        auto renderer = SequenceRenderer::create(*device_, kWidth, kHeight);
        EXPECT_TRUE(renderer.has_value())
            << (renderer.has_error() ? renderer.error().to_string() : "");
        return std::move(renderer).value();
    }

    /// One video track holding the fixture, twenty frames in.
    static Document one_clip() {
        Document document = Document::create(Rational{1, 90000}, Rational{30, 1}).value();
        const TrackId track = document.add_track(TrackKind::video, "V1").value();
        EXPECT_TRUE(document
                        .add_clip(track, fixture().string(), 20 * kFrame, 0, 20 * kFrame,
                                  60 * kFrame)
                        .has_value());
        return document;
    }

    std::unique_ptr<Instance> instance_;
    std::unique_ptr<Device> device_;
};

TEST_F(RendererTest, RendersAFrameOfRealMedia) {
    SequenceRenderer renderer = make_renderer();
    const auto image = renderer.render(one_clip(), 0);
    ASSERT_TRUE(image.has_value()) << image.error().to_string();
    EXPECT_TRUE(image.value().is_valid());
    EXPECT_EQ(image.value().width, kWidth);
    EXPECT_EQ(image.value().height, kHeight);
}

TEST_F(RendererTest, DifferentFramesOfTheTimelineAreDifferentPictures) {
    // The check that catches a renderer ignoring the playhead: testsrc2 moves,
    // so two timeline frames must not produce identical bytes. Without this,
    // rendering frame 0 for every request would pass every other assertion here.
    SequenceRenderer renderer = make_renderer();
    const Document document = one_clip();

    const auto first = renderer.render(document, 0);
    const auto later = renderer.render(document, 15);
    ASSERT_TRUE(first.has_value()) << first.error().to_string();
    ASSERT_TRUE(later.has_value()) << later.error().to_string();
    EXPECT_NE(first.value().pixels, later.value().pixels);
}

TEST_F(RendererTest, TheSameFrameTwiceIsTheSamePicture) {
    // Rendering is a pure function of the document and the frame. If it were
    // not, scrubbing back and forth would show different pictures at one
    // position, and no test of a single frame would ever notice.
    SequenceRenderer renderer = make_renderer();
    const Document document = one_clip();

    const auto once = renderer.render(document, 7);
    const auto twice = renderer.render(document, 7);
    ASSERT_TRUE(once.has_value()) << once.error().to_string();
    ASSERT_TRUE(twice.has_value()) << twice.error().to_string();
    EXPECT_EQ(once.value().pixels, twice.value().pixels);
}

TEST_F(RendererTest, AGapRendersBlackRatherThanTheLastFrame) {
    // Holding the previous picture would make a stale frame indistinguishable
    // from a live one -- the same class of lie as a placeholder frame.
    SequenceRenderer renderer = make_renderer();
    Document document = one_clip();

    const auto picture = renderer.render(document, 5);
    ASSERT_TRUE(picture.has_value());

    const auto gap = renderer.render(document, 100);  // past the end of the clip
    ASSERT_TRUE(gap.has_value()) << gap.error().to_string();

    for (std::size_t i = 0; i + 3 < gap.value().pixels.size(); i += 4) {
        ASSERT_EQ(gap.value().pixels[i], 0) << "pixel " << i / 4 << " is not black";
        ASSERT_EQ(gap.value().pixels[i + 1], 0);
        ASSERT_EQ(gap.value().pixels[i + 2], 0);
        ASSERT_EQ(gap.value().pixels[i + 3], 255) << "and must be opaque";
    }
}

TEST_F(RendererTest, ReusesOneDecoderAcrossFramesAndAcrossClips) {
    // Reopening a file per frame would make scrubbing unusable and would show up
    // as nothing but slowness.
    SequenceRenderer renderer = make_renderer();
    Document document = Document::create(Rational{1, 90000}, Rational{30, 1}).value();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    ASSERT_TRUE(
        document.add_clip(track, fixture().string(), 0, 0, 20 * kFrame, 60 * kFrame).has_value());
    ASSERT_TRUE(document
                    .add_clip(track, fixture().string(), 20 * kFrame, 20 * kFrame, 20 * kFrame,
                              60 * kFrame)
                    .has_value());

    for (const std::int64_t frame : {0, 5, 10, 25, 30}) {
        ASSERT_TRUE(renderer.render(document, frame).has_value());
    }
    EXPECT_EQ(renderer.open_sources(), 1u) << "two clips, one file, one decoder";
}

TEST_F(RendererTest, ReportsAMissingSourceRatherThanDrawingBlack) {
    SequenceRenderer renderer = make_renderer();
    Document document = Document::create(Rational{1, 90000}, Rational{30, 1}).value();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    ASSERT_TRUE(
        document.add_clip(track, "no-such-file.mp4", 0, 0, 20 * kFrame, 60 * kFrame).has_value());

    const auto rendered = renderer.render(document, 0);
    ASSERT_TRUE(rendered.has_error()) << "a missing file is not an empty frame";
    EXPECT_NE(rendered.error().to_string().find("no-such-file.mp4"), std::string::npos)
        << rendered.error().to_string();
}

TEST_F(RendererTest, RefusesASourceThatIsNotTheSequenceSize) {
    // Stretching silently would hide a mismatched import. Scaling is transform
    // work and belongs with the keyframe system (M6).
    auto renderer = SequenceRenderer::create(*device_, 1920, 1080);
    ASSERT_TRUE(renderer.has_value());

    const auto rendered = renderer.value().render(one_clip(), 0);
    ASSERT_TRUE(rendered.has_error());
    EXPECT_EQ(rendered.error().code(), Errc::unsupported_format);
    EXPECT_NE(rendered.error().to_string().find("320x240"), std::string::npos)
        << rendered.error().to_string();
}

TEST_F(RendererTest, RefusesANonsenseSequenceSize) {
    EXPECT_TRUE(SequenceRenderer::create(*device_, 0, 240).has_error());
    EXPECT_TRUE(SequenceRenderer::create(*device_, 320, -1).has_error());
}

}  // namespace
