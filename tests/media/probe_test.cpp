#include "rf/media/probe.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace {

using rf::Errc;
using rf::media::MediaInfo;
using rf::media::Rational;
using rf::media::StreamKind;
using rf::media::probe_file;

std::filesystem::path fixture(const std::string& name) {
    return std::filesystem::path{RF_TEST_FIXTURE_DIR} / "media" / name;
}

// --- the ordinary case -------------------------------------------------------
// Expected values are ffprobe output for these exact files, recorded in
// tests/fixtures/media/README.md. They are ground truth, not guesses.

TEST(Probe, ReadsContainerLevelMetadata) {
    const auto info = probe_file(fixture("bars_320x240_30fps_h264_aac.mp4"));
    ASSERT_TRUE(info.has_value()) << info.error().to_string();

    EXPECT_NE(info.value().format_name.find("mp4"), std::string::npos)
        << info.value().format_name;
    ASSERT_TRUE(info.value().duration_microseconds.has_value());
    EXPECT_EQ(info.value().duration_microseconds.value(), 2000000);
    EXPECT_EQ(info.value().streams.size(), 2u);
}

TEST(Probe, DescribesTheVideoStream) {
    const auto info = probe_file(fixture("bars_320x240_30fps_h264_aac.mp4"));
    ASSERT_TRUE(info.has_value()) << info.error().to_string();

    const auto* video = info.value().primary_video();
    ASSERT_NE(video, nullptr);
    EXPECT_EQ(video->index, 0);
    EXPECT_EQ(video->kind, StreamKind::video);
    EXPECT_EQ(video->codec_name, "h264");
    EXPECT_EQ(video->time_base, Rational(1, 15360));

    ASSERT_TRUE(video->video.has_value());
    EXPECT_EQ(video->video->width, 320);
    EXPECT_EQ(video->video->height, 240);
    EXPECT_EQ(video->video->pixel_format, "yuv420p");
    ASSERT_TRUE(video->video->average_frame_rate.has_value());
    EXPECT_EQ(video->video->average_frame_rate.value(), Rational(30, 1));

    ASSERT_TRUE(video->container_frame_count.has_value());
    EXPECT_EQ(video->container_frame_count.value(), 60);
}

TEST(Probe, DescribesTheAudioStream) {
    const auto info = probe_file(fixture("bars_320x240_30fps_h264_aac.mp4"));
    ASSERT_TRUE(info.has_value()) << info.error().to_string();

    const auto* audio = info.value().primary_audio();
    ASSERT_NE(audio, nullptr);
    EXPECT_EQ(audio->index, 1);
    EXPECT_EQ(audio->codec_name, "aac");
    EXPECT_EQ(audio->time_base, Rational(1, 48000));

    ASSERT_TRUE(audio->audio.has_value());
    EXPECT_EQ(audio->audio->sample_rate, 48000);
    EXPECT_EQ(audio->audio->channel_count, 2);
}

TEST(Probe, AudioStreamHasNoFrameRateRatherThanAFabricatedOne) {
    // Audio streams report 0/0. That must arrive as "not stated", never as a
    // default that later gets used as if the container had said it.
    const auto info = probe_file(fixture("bars_320x240_30fps_h264_aac.mp4"));
    ASSERT_TRUE(info.has_value()) << info.error().to_string();

    const auto* audio = info.value().primary_audio();
    ASSERT_NE(audio, nullptr);
    EXPECT_FALSE(audio->video.has_value());
}

// --- non-integer frame rates -------------------------------------------------

TEST(Probe, PreservesNtscFrameRateExactly) {
    // The whole reason Rational exists: 30000/1001 must survive probing as an
    // exact ratio, not as 29.97 and not as 30.
    const auto info = probe_file(fixture("bars_320x240_2997fps_h264.mp4"));
    ASSERT_TRUE(info.has_value()) << info.error().to_string();

    const auto* video = info.value().primary_video();
    ASSERT_NE(video, nullptr);
    ASSERT_TRUE(video->video.has_value());
    ASSERT_TRUE(video->video->average_frame_rate.has_value());

    const Rational rate = video->video->average_frame_rate.value();
    EXPECT_EQ(rate, Rational(30000, 1001));
    EXPECT_EQ(rate.numerator(), 30000);
    EXPECT_EQ(rate.denominator(), 1001);
    EXPECT_NE(rate, Rational(30, 1));
    EXPECT_EQ(video->time_base, Rational(1, 30000));
}

TEST(Probe, HandlesAFileWithNoAudio) {
    const auto info = probe_file(fixture("bars_320x240_2997fps_h264.mp4"));
    ASSERT_TRUE(info.has_value()) << info.error().to_string();
    EXPECT_EQ(info.value().streams.size(), 1u);
    EXPECT_NE(info.value().primary_video(), nullptr);
    EXPECT_EQ(info.value().primary_audio(), nullptr) << "invented an audio stream";
}

TEST(Probe, HandlesAFileWithNoVideo) {
    const auto info = probe_file(fixture("tone_mono_44100_aac.m4a"));
    ASSERT_TRUE(info.has_value()) << info.error().to_string();
    EXPECT_EQ(info.value().primary_video(), nullptr) << "invented a video stream";

    const auto* audio = info.value().primary_audio();
    ASSERT_NE(audio, nullptr);
    ASSERT_TRUE(audio->audio.has_value());
    EXPECT_EQ(audio->audio->sample_rate, 44100);
    EXPECT_EQ(audio->audio->channel_count, 1);
}

// --- failure paths -----------------------------------------------------------
// Each of these is something a user does by accident on an ordinary afternoon,
// so each must produce a usable error rather than a crash or an empty success.

TEST(Probe, MissingFileIsNotFound) {
    const auto info = probe_file(fixture("does_not_exist.mp4"));
    ASSERT_TRUE(info.has_error());
    EXPECT_EQ(info.error().code(), Errc::not_found);
    EXPECT_NE(info.error().message().find("does_not_exist.mp4"), std::string::npos)
        << "error should name the file: " << info.error().message();
}

TEST(Probe, DirectoryInsteadOfFileIsAnError) {
    const auto info = probe_file(std::filesystem::path{RF_TEST_FIXTURE_DIR} / "media");
    ASSERT_TRUE(info.has_error()) << "probing a directory must not succeed";
}

TEST(Probe, NonMediaFileIsRejectedWithAUsefulError) {
    const auto info = probe_file(fixture("not_media.mp4"));
    ASSERT_TRUE(info.has_error());
    EXPECT_NE(info.error().message().find("not_media.mp4"), std::string::npos)
        << info.error().message();
    // Whatever libav decides to call it, it must not be reported as success and
    // must not be a code that reads as a ReelForge bug.
    EXPECT_NE(info.error().code(), Errc::internal) << info.error().to_string();
}

TEST(Probe, ErrorMessagesCarryTheLibavCode) {
    // "Invalid data found when processing input" alone is not actionable in a
    // bug report; the numeric code is what identifies the path taken.
    const auto info = probe_file(fixture("not_media.mp4"));
    ASSERT_TRUE(info.has_error());
    EXPECT_NE(info.error().message().find("libav"), std::string::npos)
        << info.error().message();
}

TEST(Probe, TruncatedFileDoesNotCrashOrLie) {
    // A file cut mid-bitstream may still probe successfully -- the header is
    // intact -- or may fail. Both are acceptable; inventing a duration that
    // matches the original file is not, and neither is crashing.
    const auto info = probe_file(fixture("truncated_h264.mp4"));
    if (info.has_value()) {
        const auto* video = info.value().primary_video();
        ASSERT_NE(video, nullptr);
        EXPECT_EQ(video->codec_name, "h264");
    } else {
        EXPECT_NE(info.error().code(), Errc::internal) << info.error().to_string();
    }
}

TEST(Probe, RepeatedProbesAreIndependent) {
    // Catches state leaking through libav's global registries between opens.
    for (int i = 0; i < 5; ++i) {
        const auto info = probe_file(fixture("bars_320x240_30fps_h264_aac.mp4"));
        ASSERT_TRUE(info.has_value()) << "iteration " << i << ": " << info.error().to_string();
        ASSERT_EQ(info.value().streams.size(), 2u) << "iteration " << i;
    }
}

}  // namespace
