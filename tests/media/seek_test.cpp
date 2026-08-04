// Frame-accurate seek.
//
// The oracle here is linear decode. Seeking to frame N and decoding forward to
// frame N are two genuinely different paths through this code -- one flushes the
// decoder and restarts from a keyframe, the other never does -- so agreement
// between them is a real check on the part ReelForge is responsible for. What it
// does not test is whether libav decodes H.264 correctly, which was never
// ReelForge's claim to make.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "rf/media/decoder.hpp"
#include "rf/media/video_frame.hpp"

namespace {

using rf::Errc;
using rf::media::VideoDecoder;
using rf::media::VideoFrame;
using rf::media::frame_hash;

constexpr const char* kCfrFixture = "bars_320x240_30fps_h264_aac.mp4";
constexpr const char* kNtscFixture = "bars_320x240_2997fps_h264.mp4";
constexpr std::int64_t kFixtureFrames = 60;

std::filesystem::path fixture(const std::string& name) {
    return std::filesystem::path{RF_TEST_FIXTURE_DIR} / "media" / name;
}

struct Reference {
    std::vector<std::uint64_t> hashes;
    std::vector<std::int64_t> timestamps;
};

/// Decodes the file start to finish, recording one hash and one timestamp per
/// frame. This is the reference every seek is measured against.
Reference build_reference(const std::string& name) {
    Reference reference;
    auto decoder = VideoDecoder::open(fixture(name));
    EXPECT_TRUE(decoder.has_value()) << (decoder.has_error() ? decoder.error().to_string() : "");
    if (!decoder) {
        return reference;
    }
    for (;;) {
        auto frame = decoder.value().next_frame();
        EXPECT_TRUE(frame.has_value()) << (frame.has_error() ? frame.error().to_string() : "");
        if (!frame || !frame.value().has_value()) {
            break;
        }
        const VideoFrame& decoded = frame.value().value();
        reference.hashes.push_back(frame_hash(decoded));
        reference.timestamps.push_back(decoded.presentation_timestamp.value_or(-1));
    }
    return reference;
}

/// Seeks to `index` and returns the hash of the frame that comes back.
::testing::AssertionResult seek_and_hash(VideoDecoder& decoder, std::int64_t index,
                                         std::uint64_t& hash_out, std::int64_t& timestamp_out) {
    if (auto sought = decoder.seek_to_frame(index); !sought) {
        return ::testing::AssertionFailure()
               << "seek to frame " << index << " failed: " << sought.error().to_string();
    }
    auto frame = decoder.next_frame();
    if (!frame) {
        return ::testing::AssertionFailure()
               << "decode after seek to " << index << " failed: " << frame.error().to_string();
    }
    if (!frame.value().has_value()) {
        return ::testing::AssertionFailure() << "seek to frame " << index << " produced no frame";
    }
    const VideoFrame& decoded = frame.value().value();
    if (decoded.frame_index != index) {
        return ::testing::AssertionFailure()
               << "frame index after seek to " << index << " was "
               << decoded.frame_index.value_or(-1);
    }
    hash_out = frame_hash(decoded);
    timestamp_out = decoded.presentation_timestamp.value_or(-1);
    return ::testing::AssertionSuccess();
}

void expect_every_frame_seekable(const std::string& name) {
    const Reference reference = build_reference(name);
    ASSERT_EQ(reference.hashes.size(), static_cast<std::size_t>(kFixtureFrames)) << name;

    auto decoder = VideoDecoder::open(fixture(name));
    ASSERT_TRUE(decoder.has_value()) << decoder.error().to_string();

    for (std::int64_t index = 0; index < kFixtureFrames; ++index) {
        std::uint64_t hash = 0;
        std::int64_t timestamp = 0;
        ASSERT_TRUE(seek_and_hash(decoder.value(), index, hash, timestamp)) << name;
        ASSERT_EQ(hash, reference.hashes[static_cast<std::size_t>(index)])
            << name << ": seeking to frame " << index << " produced a different frame than "
            << "decoding to it linearly";
        ASSERT_EQ(timestamp, reference.timestamps[static_cast<std::size_t>(index)])
            << name << ": timestamp mismatch at frame " << index;
    }
}

// --- the gate check ----------------------------------------------------------

TEST(Seek, EveryFrameOfACfrFileMatchesLinearDecode) {
    expect_every_frame_seekable(kCfrFixture);
}

TEST(Seek, EveryFrameOfAnNtscFileMatchesLinearDecode) {
    // 30000/1001 is the case a floating-point seek computation gets wrong, and
    // it gets it wrong by exactly one frame, intermittently, deep into the file.
    expect_every_frame_seekable(kNtscFixture);
}

// --- seek order --------------------------------------------------------------

TEST(Seek, BackwardSeeksAreAccurate) {
    // Scrubbing right to left is the case where stale decoder state shows up:
    // without flushing, frames from the previous position leak through.
    const Reference reference = build_reference(kNtscFixture);
    ASSERT_EQ(reference.hashes.size(), static_cast<std::size_t>(kFixtureFrames));

    auto decoder = VideoDecoder::open(fixture(kNtscFixture));
    ASSERT_TRUE(decoder.has_value()) << decoder.error().to_string();

    for (std::int64_t index = kFixtureFrames - 1; index >= 0; --index) {
        std::uint64_t hash = 0;
        std::int64_t timestamp = 0;
        ASSERT_TRUE(seek_and_hash(decoder.value(), index, hash, timestamp));
        ASSERT_EQ(hash, reference.hashes[static_cast<std::size_t>(index)])
            << "backward seek to frame " << index;
    }
}

TEST(Seek, RandomOrderSeeksAreAccurate) {
    const Reference reference = build_reference(kCfrFixture);
    ASSERT_EQ(reference.hashes.size(), static_cast<std::size_t>(kFixtureFrames));

    std::vector<std::int64_t> order(static_cast<std::size_t>(kFixtureFrames));
    std::iota(order.begin(), order.end(), 0);
    std::mt19937 generator{20260804u};  // Fixed seed: a failure must be reproducible.
    std::shuffle(order.begin(), order.end(), generator);

    auto decoder = VideoDecoder::open(fixture(kCfrFixture));
    ASSERT_TRUE(decoder.has_value()) << decoder.error().to_string();

    for (const std::int64_t index : order) {
        std::uint64_t hash = 0;
        std::int64_t timestamp = 0;
        ASSERT_TRUE(seek_and_hash(decoder.value(), index, hash, timestamp));
        ASSERT_EQ(hash, reference.hashes[static_cast<std::size_t>(index)])
            << "random-order seek to frame " << index;
    }
}

TEST(Seek, RepeatedSeeksToTheSameFrameAreStable) {
    const Reference reference = build_reference(kCfrFixture);
    ASSERT_FALSE(reference.hashes.empty());

    auto decoder = VideoDecoder::open(fixture(kCfrFixture));
    ASSERT_TRUE(decoder.has_value()) << decoder.error().to_string();

    for (int attempt = 0; attempt < 4; ++attempt) {
        std::uint64_t hash = 0;
        std::int64_t timestamp = 0;
        ASSERT_TRUE(seek_and_hash(decoder.value(), 37, hash, timestamp)) << "attempt " << attempt;
        ASSERT_EQ(hash, reference.hashes[37]) << "attempt " << attempt;
    }
}

// --- decoding onward from a seek ---------------------------------------------

TEST(Seek, DecodingContinuesInOrderAfterASeek) {
    // A seek that lands correctly but then renumbers or skips the frames after
    // it would break every ripple edit built on top of this.
    const Reference reference = build_reference(kNtscFixture);
    ASSERT_EQ(reference.hashes.size(), static_cast<std::size_t>(kFixtureFrames));

    auto decoder = VideoDecoder::open(fixture(kNtscFixture));
    ASSERT_TRUE(decoder.has_value()) << decoder.error().to_string();
    ASSERT_TRUE(decoder.value().seek_to_frame(20).has_value());

    for (std::int64_t expected = 20; expected < kFixtureFrames; ++expected) {
        auto frame = decoder.value().next_frame();
        ASSERT_TRUE(frame.has_value()) << frame.error().to_string();
        ASSERT_TRUE(frame.value().has_value()) << "stream ended early at " << expected;

        const VideoFrame& decoded = frame.value().value();
        ASSERT_EQ(decoded.frame_index.value_or(-1), expected);
        ASSERT_EQ(frame_hash(decoded), reference.hashes[static_cast<std::size_t>(expected)])
            << "frame " << expected << " after a seek to 20";
    }

    auto past_end = decoder.value().next_frame();
    ASSERT_TRUE(past_end.has_value()) << past_end.error().to_string();
    EXPECT_FALSE(past_end.value().has_value());
}

TEST(Seek, SeekingToZeroRewindsAFullyDrainedDecoder) {
    // Reaching end of stream sets the decoder's finished flag; a seek must clear
    // it, or the playhead can never return to the start of a clip.
    const Reference reference = build_reference(kCfrFixture);
    ASSERT_FALSE(reference.hashes.empty());

    auto decoder = VideoDecoder::open(fixture(kCfrFixture));
    ASSERT_TRUE(decoder.has_value()) << decoder.error().to_string();
    while (true) {
        auto frame = decoder.value().next_frame();
        ASSERT_TRUE(frame.has_value()) << frame.error().to_string();
        if (!frame.value().has_value()) {
            break;
        }
    }

    std::uint64_t hash = 0;
    std::int64_t timestamp = 0;
    ASSERT_TRUE(seek_and_hash(decoder.value(), 0, hash, timestamp));
    EXPECT_EQ(hash, reference.hashes[0]);
}

// --- cost of a seek ----------------------------------------------------------
// These are performance regression tests that assert on a counter rather than a
// clock, so they are deterministic, resolution-independent, and safe to run on a
// noisy CI machine. A stopwatch here would either flap or be too loose to catch
// anything.

TEST(SeekCost, MaterialisesOnlyTheFrameActuallyRequested) {
    // A seek decodes every frame from the preceding keyframe to the target, but
    // must copy only the one it was asked for. Copying the discarded frames too
    // is invisible to every correctness test in this file and cost a 5.8x seek
    // slowdown at 4K before measurement found it.
    auto decoder = VideoDecoder::open(fixture(kCfrFixture));
    ASSERT_TRUE(decoder.has_value()) << decoder.error().to_string();

    // Frame 44 sits well inside a GOP, so reaching it decodes several frames.
    ASSERT_TRUE(decoder.value().seek_to_frame(44).has_value());
    EXPECT_EQ(decoder.value().frames_materialised(), 1)
        << "seeking copied frames it then discarded";

    auto frame = decoder.value().next_frame();
    ASSERT_TRUE(frame.has_value()) << frame.error().to_string();
    ASSERT_TRUE(frame.value().has_value());
    EXPECT_EQ(decoder.value().frames_materialised(), 1)
        << "the pending frame was copied twice";
}

TEST(SeekCost, RepeatedSeeksDoNotAccumulateCopies) {
    auto decoder = VideoDecoder::open(fixture(kNtscFixture));
    ASSERT_TRUE(decoder.has_value()) << decoder.error().to_string();

    constexpr std::int64_t kSeeks = 10;
    for (std::int64_t i = 0; i < kSeeks; ++i) {
        ASSERT_TRUE(decoder.value().seek_to_frame(i * 5 + 3).has_value());
        auto frame = decoder.value().next_frame();
        ASSERT_TRUE(frame.has_value()) << frame.error().to_string();
        ASSERT_TRUE(frame.value().has_value());
    }
    EXPECT_EQ(decoder.value().frames_materialised(), kSeeks)
        << "each seek should copy exactly one frame";
}

TEST(SeekCost, LinearDecodeMaterialisesEachFrameExactlyOnce) {
    auto decoder = VideoDecoder::open(fixture(kCfrFixture));
    ASSERT_TRUE(decoder.has_value()) << decoder.error().to_string();

    std::int64_t frames = 0;
    for (;;) {
        auto frame = decoder.value().next_frame();
        ASSERT_TRUE(frame.has_value()) << frame.error().to_string();
        if (!frame.value().has_value()) {
            break;
        }
        ++frames;
    }
    EXPECT_EQ(frames, kFixtureFrames);
    EXPECT_EQ(decoder.value().frames_materialised(), kFixtureFrames);
}

// --- failure paths -----------------------------------------------------------

TEST(Seek, NegativeFrameIndexIsRejected) {
    auto decoder = VideoDecoder::open(fixture(kCfrFixture));
    ASSERT_TRUE(decoder.has_value()) << decoder.error().to_string();

    const auto sought = decoder.value().seek_to_frame(-1);
    ASSERT_TRUE(sought.has_error());
    EXPECT_EQ(sought.error().code(), Errc::invalid_argument);
}

TEST(Seek, SeekingPastTheEndReportsAnErrorRatherThanTheLastFrame) {
    // Silently clamping to the final frame would make an out-of-range scrub look
    // like a successful one, and the caller would never learn its model of the
    // clip's length was wrong.
    auto decoder = VideoDecoder::open(fixture(kCfrFixture));
    ASSERT_TRUE(decoder.has_value()) << decoder.error().to_string();

    const auto sought = decoder.value().seek_to_frame(100000);
    ASSERT_TRUE(sought.has_error());
    EXPECT_EQ(sought.error().code(), Errc::not_found) << sought.error().to_string();
}

TEST(Seek, TimestampSeekLandsAtOrAfterTheRequestedTime) {
    const Reference reference = build_reference(kNtscFixture);
    ASSERT_EQ(reference.timestamps.size(), static_cast<std::size_t>(kFixtureFrames));

    auto decoder = VideoDecoder::open(fixture(kNtscFixture));
    ASSERT_TRUE(decoder.has_value()) << decoder.error().to_string();

    // Halfway between frame 9 and frame 10 must land on frame 10.
    const std::int64_t between = reference.timestamps[9] + 500;
    ASSERT_TRUE(decoder.value().seek_to_timestamp(between).has_value());

    auto frame = decoder.value().next_frame();
    ASSERT_TRUE(frame.has_value()) << frame.error().to_string();
    ASSERT_TRUE(frame.value().has_value());
    EXPECT_EQ(frame.value().value().presentation_timestamp.value_or(-1), reference.timestamps[10]);
    EXPECT_EQ(frame_hash(frame.value().value()), reference.hashes[10]);
}

}  // namespace
