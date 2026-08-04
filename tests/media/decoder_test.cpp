#include "rf/media/decoder.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "rf/media/video_frame.hpp"

namespace {

using rf::Errc;
using rf::media::Rational;
using rf::media::VideoDecoder;
using rf::media::VideoFrame;
using rf::media::frame_hash;

std::filesystem::path fixture(const std::string& name) {
    return std::filesystem::path{RF_TEST_FIXTURE_DIR} / "media" / name;
}

/// Decodes a file end to end, returning one hash per frame in presentation
/// order. This is the oracle every seek claim is measured against.
std::vector<std::uint64_t> decode_all_hashes(const std::filesystem::path& path) {
    auto decoder = VideoDecoder::open(path);
    EXPECT_TRUE(decoder.has_value()) << (decoder.has_error() ? decoder.error().to_string() : "");
    if (!decoder) {
        return {};
    }

    std::vector<std::uint64_t> hashes;
    for (;;) {
        auto frame = decoder.value().next_frame();
        EXPECT_TRUE(frame.has_value()) << (frame.has_error() ? frame.error().to_string() : "");
        if (!frame || !frame.value().has_value()) {
            break;
        }
        hashes.push_back(frame_hash(frame.value().value()));
    }
    return hashes;
}

// --- opening -----------------------------------------------------------------

TEST(Decoder, OpensAFileAndDescribesItsStream) {
    auto decoder = VideoDecoder::open(fixture("bars_320x240_30fps_h264_aac.mp4"));
    ASSERT_TRUE(decoder.has_value()) << decoder.error().to_string();

    const auto& stream = decoder.value().stream();
    EXPECT_EQ(stream.codec_name, "h264");
    EXPECT_EQ(stream.time_base, Rational(1, 15360));
    ASSERT_TRUE(stream.video.has_value());
    EXPECT_EQ(stream.video->width, 320);
    EXPECT_EQ(stream.video->height, 240);
}

TEST(Decoder, SelectsTheVideoStreamFromAFileThatAlsoHasAudio) {
    auto decoder = VideoDecoder::open(fixture("bars_320x240_30fps_h264_aac.mp4"));
    ASSERT_TRUE(decoder.has_value()) << decoder.error().to_string();
    EXPECT_EQ(decoder.value().stream().index, 0);
    EXPECT_TRUE(decoder.value().stream().video.has_value());
}

TEST(Decoder, RefusesAFileWithNoVideoStream) {
    auto decoder = VideoDecoder::open(fixture("tone_mono_44100_aac.m4a"));
    ASSERT_TRUE(decoder.has_error()) << "an audio-only file has no video to decode";
    EXPECT_NE(decoder.error().code(), Errc::internal) << decoder.error().to_string();
}

TEST(Decoder, MissingFileIsNotFound) {
    auto decoder = VideoDecoder::open(fixture("does_not_exist.mp4"));
    ASSERT_TRUE(decoder.has_error());
    EXPECT_EQ(decoder.error().code(), Errc::not_found);
}

TEST(Decoder, NonMediaFileIsRejected) {
    auto decoder = VideoDecoder::open(fixture("not_media.mp4"));
    ASSERT_TRUE(decoder.has_error());
    EXPECT_NE(decoder.error().code(), Errc::internal) << decoder.error().to_string();
}

// --- decoding ----------------------------------------------------------------

TEST(Decoder, DecodesEveryFrameIncludingTheOnesHeldBackForReordering) {
    // The container claims 60 frames. A decoder that forgets to flush loses the
    // last few, because codecs with B-frames hold frames until told the stream
    // ended -- and the loss is silent.
    auto decoder = VideoDecoder::open(fixture("bars_320x240_30fps_h264_aac.mp4"));
    ASSERT_TRUE(decoder.has_value()) << decoder.error().to_string();

    std::int64_t count = 0;
    for (;;) {
        auto frame = decoder.value().next_frame();
        ASSERT_TRUE(frame.has_value()) << frame.error().to_string();
        if (!frame.value().has_value()) {
            break;
        }
        ++count;
    }
    EXPECT_EQ(count, 60);
    EXPECT_EQ(decoder.value().frames_decoded(), 60);
}

TEST(Decoder, ProducesFramesWithTheExpectedGeometryAndFormat) {
    auto decoder = VideoDecoder::open(fixture("bars_320x240_30fps_h264_aac.mp4"));
    ASSERT_TRUE(decoder.has_value()) << decoder.error().to_string();

    auto frame = decoder.value().next_frame();
    ASSERT_TRUE(frame.has_value()) << frame.error().to_string();
    ASSERT_TRUE(frame.value().has_value());

    const VideoFrame& first = frame.value().value();
    EXPECT_EQ(first.width(), 320);
    EXPECT_EQ(first.height(), 240);
    EXPECT_EQ(first.pixel_format(), "yuv420p");
    EXPECT_FALSE(first.is_empty());

    // yuv420p is one full-resolution luma plane plus two half-resolution chroma
    // planes: 320*240 + 2*(160*120). Tight packing means exactly that, no more.
    EXPECT_EQ(first.pixels().size(), static_cast<std::size_t>(320 * 240 + 2 * 160 * 120));
}

TEST(Decoder, AssignsSequentialFrameIndices) {
    auto decoder = VideoDecoder::open(fixture("bars_320x240_30fps_h264_aac.mp4"));
    ASSERT_TRUE(decoder.has_value()) << decoder.error().to_string();

    std::int64_t expected = 0;
    for (;;) {
        auto frame = decoder.value().next_frame();
        ASSERT_TRUE(frame.has_value()) << frame.error().to_string();
        if (!frame.value().has_value()) {
            break;
        }
        ASSERT_TRUE(frame.value().value().frame_index.has_value());
        EXPECT_EQ(frame.value().value().frame_index.value(), expected);
        ++expected;
    }
}

TEST(Decoder, TimestampsAreMonotonicAndInTheStreamTimeBase) {
    auto decoder = VideoDecoder::open(fixture("bars_320x240_2997fps_h264.mp4"));
    ASSERT_TRUE(decoder.has_value()) << decoder.error().to_string();

    const Rational expected_base = decoder.value().stream().time_base;
    std::optional<std::int64_t> previous;
    std::int64_t frames = 0;

    for (;;) {
        auto frame = decoder.value().next_frame();
        ASSERT_TRUE(frame.has_value()) << frame.error().to_string();
        if (!frame.value().has_value()) {
            break;
        }
        const VideoFrame& decoded = frame.value().value();
        EXPECT_EQ(decoded.time_base, expected_base);
        ASSERT_TRUE(decoded.presentation_timestamp.has_value())
            << "frame " << frames << " has no timestamp";
        if (previous.has_value()) {
            EXPECT_GT(decoded.presentation_timestamp.value(), previous.value())
                << "timestamps went backwards at frame " << frames;
        }
        previous = decoded.presentation_timestamp;
        ++frames;
    }
    EXPECT_EQ(frames, 60);
}

TEST(Decoder, NtscFrameSpacingIsExactlyOneFrameDuration) {
    // 1/(30000/1001) of a second in a 1/30000 time base is exactly 1001 ticks.
    // Any drift here means the decoder or the time base plumbing is lying.
    auto decoder = VideoDecoder::open(fixture("bars_320x240_2997fps_h264.mp4"));
    ASSERT_TRUE(decoder.has_value()) << decoder.error().to_string();

    std::optional<std::int64_t> previous;
    while (true) {
        auto frame = decoder.value().next_frame();
        ASSERT_TRUE(frame.has_value()) << frame.error().to_string();
        if (!frame.value().has_value()) {
            break;
        }
        const auto timestamp = frame.value().value().presentation_timestamp;
        ASSERT_TRUE(timestamp.has_value());
        if (previous.has_value()) {
            EXPECT_EQ(timestamp.value() - previous.value(), 1001);
        }
        previous = timestamp;
    }
}

TEST(Decoder, ReturnsEndOfStreamRepeatedlyWithoutError) {
    // Calling past the end must stay an empty optional, not become an error and
    // not start handing back stale frames.
    auto decoder = VideoDecoder::open(fixture("tone_mono_44100_aac.m4a"));
    ASSERT_TRUE(decoder.has_error());

    auto video = VideoDecoder::open(fixture("bars_320x240_2997fps_h264.mp4"));
    ASSERT_TRUE(video.has_value()) << video.error().to_string();
    while (true) {
        auto frame = video.value().next_frame();
        ASSERT_TRUE(frame.has_value()) << frame.error().to_string();
        if (!frame.value().has_value()) {
            break;
        }
    }
    for (int i = 0; i < 3; ++i) {
        auto frame = video.value().next_frame();
        ASSERT_TRUE(frame.has_value()) << frame.error().to_string();
        EXPECT_FALSE(frame.value().has_value()) << "produced a frame after end of stream";
    }
    EXPECT_EQ(video.value().frames_decoded(), 60);
}

TEST(Decoder, SurvivesATruncatedFile) {
    // Either it decodes what is there and stops, or it reports a real error.
    // What it must not do is crash, hang, or invent frames.
    auto decoder = VideoDecoder::open(fixture("truncated_h264.mp4"));
    if (!decoder) {
        EXPECT_NE(decoder.error().code(), Errc::internal) << decoder.error().to_string();
        return;
    }
    std::int64_t frames = 0;
    for (;;) {
        auto frame = decoder.value().next_frame();
        if (!frame) {
            EXPECT_NE(frame.error().code(), Errc::internal) << frame.error().to_string();
            break;
        }
        if (!frame.value().has_value()) {
            break;
        }
        ++frames;
        ASSERT_LT(frames, 1000) << "decoded more frames than the source could contain";
    }
    EXPECT_LE(frames, 60) << "a half-length file produced a full-length decode";
}

// --- hashing -----------------------------------------------------------------

TEST(FrameHash, IsDeterministicAcrossDecodes) {
    // The property the whole seek gate depends on: decoding the same file twice
    // yields identical hashes, so a difference later means a real difference.
    const auto first = decode_all_hashes(fixture("bars_320x240_30fps_h264_aac.mp4"));
    const auto second = decode_all_hashes(fixture("bars_320x240_30fps_h264_aac.mp4"));
    ASSERT_EQ(first.size(), 60u);
    EXPECT_EQ(first, second);
}

TEST(FrameHash, DistinguishesDifferentFrames) {
    // testsrc2 has a moving element, so consecutive frames must differ. If they
    // did not, the hash would be useless as a seek oracle and every seek test
    // would pass vacuously.
    const auto hashes = decode_all_hashes(fixture("bars_320x240_30fps_h264_aac.mp4"));
    ASSERT_EQ(hashes.size(), 60u);
    const std::set<std::uint64_t> distinct(hashes.begin(), hashes.end());
    EXPECT_EQ(distinct.size(), hashes.size()) << "frames are not distinguishable by hash";
}

TEST(FrameHash, DependsOnGeometryNotOnlyPixels) {
    const VideoFrame wide{4, 1, "gray", std::vector<std::uint8_t>{1, 2, 3, 4}};
    const VideoFrame tall{1, 4, "gray", std::vector<std::uint8_t>{1, 2, 3, 4}};
    EXPECT_NE(frame_hash(wide), frame_hash(tall));
}

TEST(FrameHash, DependsOnPixelFormat) {
    const VideoFrame a{2, 1, "gray", std::vector<std::uint8_t>{1, 2}};
    const VideoFrame b{2, 1, "ya8", std::vector<std::uint8_t>{1, 2}};
    EXPECT_NE(frame_hash(a), frame_hash(b));
}

TEST(FrameHash, EqualFramesHashEqual) {
    const VideoFrame a{2, 2, "gray", std::vector<std::uint8_t>{9, 8, 7, 6}};
    const VideoFrame b{2, 2, "gray", std::vector<std::uint8_t>{9, 8, 7, 6}};
    EXPECT_EQ(frame_hash(a), frame_hash(b));
}

}  // namespace
