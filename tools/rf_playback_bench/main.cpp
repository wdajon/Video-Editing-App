// rf_playback_bench -- can the compositor sustain the frame rate the timeline asks for?
//
// M3's gate is "1080x1920, 3 layers, sustained 30 fps, no dropped frames over
// 60 s". This measures exactly that, against clock time rather than an average:
// 1,800 frames in 60 seconds averages 30 fps and can stutter throughout, so the
// numbers reported here are drops and percentiles, never a mean frame rate.
//
// It composites only. Decoding and presentation are not in this loop, so a pass
// here is necessary for the gate and not sufficient for it.
//
//   rf_playback_bench [--width N] [--height N] [--layers N] [--seconds N] [--fps N]
//
// Exits non-zero if any frame is dropped or p99 exceeds the frame budget.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rf/gpu/compositor.hpp"
#include "rf/gpu/device.hpp"
#include "rf/gpu/instance.hpp"
#include "rf/gpu/texture.hpp"
#include "rf/media/convert.hpp"
#include "rf/media/decoder.hpp"
#include "rf/media/rational.hpp"
#include "rf/playback/clock.hpp"
#include "rf/playback/frame_log.hpp"
#include "rf/playback/pacer.hpp"
#include "rf/playback/playback_clock.hpp"

namespace {

struct Options {
    int width = 1080;
    int height = 1920;
    int layers = 3;
    int seconds = 60;
    int fps = 30;

    /// When set, layer 0 comes from this file instead of a synthetic image:
    /// decode, convert to RGBA, upload, composite. That is the real workload,
    /// and it is where decode cost shows up against the frame budget.
    std::string video;
};

bool parse(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view flag = argv[i];
        if (i + 1 >= argc) {
            std::fprintf(stderr, "missing value for %.*s\n", static_cast<int>(flag.size()),
                         flag.data());
            return false;
        }
        if (flag == "--video") {
            options.video = argv[++i];
            continue;
        }
        const int value = std::atoi(argv[++i]);
        if (flag == "--width") { options.width = value; }
        else if (flag == "--height") { options.height = value; }
        else if (flag == "--layers") { options.layers = value; }
        else if (flag == "--seconds") { options.seconds = value; }
        else if (flag == "--fps") { options.fps = value; }
        else {
            std::fprintf(stderr, "unknown flag: %.*s\n", static_cast<int>(flag.size()),
                         flag.data());
            return false;
        }
    }
    return options.width > 0 && options.height > 0 && options.layers > 0 &&
           options.seconds > 0 && options.fps > 0;
}

rf::gpu::ImageRgba8 make_layer(int width, int height, int index) {
    rf::gpu::ImageRgba8 image;
    image.width = width;
    image.height = height;
    image.pixels.resize(image.expected_size());
    // Distinct per layer so the composite is doing real blending rather than
    // repeatedly writing the same value.
    const auto base = static_cast<std::uint8_t>(40 + index * 37);
    for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
        image.pixels[i + 0] = base;
        image.pixels[i + 1] = static_cast<std::uint8_t>(255 - base);
        image.pixels[i + 2] = static_cast<std::uint8_t>((base * 3) % 256);
        image.pixels[i + 3] = index == 0 ? 255u : 160u;
    }
    return image;
}

double to_ms(rf::playback::Nanoseconds value) {
    return std::chrono::duration<double, std::milli>(value).count();
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse(argc, argv, options)) {
        std::fprintf(stderr,
                     "usage: rf_playback_bench [--width N] [--height N] [--layers N]"
                     " [--seconds N] [--fps N]\n");
        return 2;
    }

    if (!rf::gpu::vulkan_available()) {
        std::fprintf(stderr, "no Vulkan loader on this machine\n");
        return 2;
    }

    rf::gpu::Instance::Options instance_options;
    auto instance = rf::gpu::Instance::create(instance_options);
    if (!instance) {
        std::fprintf(stderr, "%s\n", instance.error().to_string().c_str());
        return 2;
    }
    auto device = rf::gpu::Device::create_preferred(instance.value());
    if (!device) {
        std::fprintf(stderr, "%s\n", device.error().to_string().c_str());
        return 2;
    }
    auto compositor = rf::gpu::Compositor::create(device.value());
    if (!compositor) {
        std::fprintf(stderr, "%s\n", compositor.error().to_string().c_str());
        return 2;
    }

    std::printf("device:  %s (%s)\n", device.value().info().name.c_str(),
                std::string{to_string(device.value().info().kind)}.c_str());
    std::printf("scene:   %dx%d, %d layers, %d fps for %d s\n", options.width, options.height,
                options.layers, options.fps, options.seconds);
    if (device.value().info().kind == rf::gpu::DeviceKind::software) {
        std::printf("\nNOTE: this is a software Vulkan device. The numbers below say nothing\n"
                    "      about the frame-rate gate; run this on real hardware.\n");
    }
    std::fflush(stdout);

    // Layers are uploaded once and stay on the device. That is the entire point
    // of the playback path: a still layer never re-uploads, and a video layer
    // uploads only when a new frame is decoded. Nothing is read back.
    std::vector<rf::gpu::Texture> textures;
    std::vector<rf::gpu::GpuLayer> layers;
    textures.reserve(static_cast<std::size_t>(options.layers));
    layers.reserve(static_cast<std::size_t>(options.layers));
    for (int i = 0; i < options.layers; ++i) {
        auto texture = rf::gpu::Texture::create_from(
            device.value(), make_layer(options.width, options.height, i));
        if (!texture) {
            std::fprintf(stderr, "%s\n", texture.error().to_string().c_str());
            return 1;
        }
        textures.push_back(std::move(texture).value());
    }
    for (int i = 0; i < options.layers; ++i) {
        layers.push_back({&textures[static_cast<std::size_t>(i)], i == 0 ? 1.0F : 0.8F, true});
    }

    auto target = rf::gpu::Texture::create(device.value(), options.width, options.height);
    if (!target) {
        std::fprintf(stderr, "%s\n", target.error().to_string().c_str());
        return 1;
    }

    auto clock = rf::playback::PlaybackClock::create(rf::media::Rational{options.fps, 1});
    if (!clock) {
        std::fprintf(stderr, "%s\n", clock.error().to_string().c_str());
        return 2;
    }

    const std::int64_t total_frames =
        static_cast<std::int64_t>(options.seconds) * static_cast<std::int64_t>(options.fps);
    rf::playback::FrameLog log(static_cast<std::size_t>(total_frames));
    rf::playback::SteadyClock wall;

    // One composite before timing starts: the first submission on a fresh
    // pipeline pays for shader compilation in the driver and would otherwise
    // land in the measurement as a spike that is not representative.
    if (auto warmup = compositor.value().composite_into(target.value(), layers); !warmup) {
        std::fprintf(stderr, "warm-up composite failed: %s\n", warmup.error().to_string().c_str());
        return 1;
    }

    const auto start = wall.now();
    if (auto played = clock.value().play(start, rf::media::Rational{1, 1}); !played) {
        std::fprintf(stderr, "%s\n", played.error().to_string().c_str());
        return 2;
    }

    // Paced: wait until each frame is due, then render whatever the clock says
    // should be on screen. Without this the loop free-runs, never falls behind,
    // and its zero-drop result measures throughput rather than playback.
    rf::playback::Pacer pacer{wall, clock.value(), log};

    // Opened after the warm-up so the first decode is inside the measurement,
    // where it belongs -- a decoder that stalls on its first frame is a real
    // playback defect.
    std::optional<rf::media::VideoDecoder> decoder;
    if (!options.video.empty()) {
        auto opened = rf::media::VideoDecoder::open(options.video);
        if (!opened) {
            std::fprintf(stderr, "%s\n", opened.error().to_string().c_str());
            return 2;
        }
        decoder = std::move(opened).value();
    }

    std::int64_t decoded_frames = 0;
    std::int64_t exhausted_at = -1;

    for (std::int64_t frame = 0; frame < total_frames; ++frame) {
        auto tick = pacer.wait_next();
        if (!tick) {
            std::fprintf(stderr, "%s\n", tick.error().to_string().c_str());
            return 2;
        }

        if (decoder.has_value()) {
            auto next = decoder->next_frame();
            if (!next) {
                std::fprintf(stderr, "decode failed: %s\n", next.error().to_string().c_str());
                return 1;
            }
            if (next.value().has_value()) {
                auto rgba = rf::media::to_rgba8(next.value().value());
                if (!rgba) {
                    std::fprintf(stderr, "%s\n", rgba.error().to_string().c_str());
                    return 1;
                }
                rf::gpu::ImageRgba8 image;
                image.width = rgba.value().width();
                image.height = rgba.value().height();
                image.pixels = rgba.value().pixels();
                if (auto uploaded = textures[0].upload(image); !uploaded) {
                    std::fprintf(stderr, "%s\n", uploaded.error().to_string().c_str());
                    return 1;
                }
                ++decoded_frames;
            } else if (exhausted_at < 0) {
                // Running out of source mid-run would otherwise look like a
                // sudden speed-up, so it is recorded and reported.
                exhausted_at = frame;
            }
        }

        auto composed = compositor.value().composite_into(target.value(), layers);
        if (!composed) {
            std::fprintf(stderr, "composite failed at frame %lld: %s\n",
                         static_cast<long long>(tick.value().frame),
                         composed.error().to_string().c_str());
            return 1;
        }

        pacer.presented(tick.value(), wall.now());
    }

    const auto elapsed = wall.now() - start;
    const rf::playback::Nanoseconds budget{1'000'000'000LL / options.fps};
    const rf::playback::FrameStatistics stats = log.statistics(budget);

    std::printf("\nproduced %lld frames in %.1f s\n", static_cast<long long>(stats.presented),
                std::chrono::duration<double>(elapsed).count());
    if (!options.video.empty()) {
        std::printf("decoded:  %lld frames from %s\n", static_cast<long long>(decoded_frames),
                    options.video.c_str());
        if (exhausted_at >= 0) {
            std::printf("  WARNING: source ran out at frame %lld; the rest of the run\n"
                        "           composited a stale layer and is not representative\n",
                        static_cast<long long>(exhausted_at));
        }
    }
    std::printf("dropped:  %lld\n", static_cast<long long>(stats.dropped));
    std::printf("late:     %lld (later than one frame period)\n",
                static_cast<long long>(stats.late));
    std::printf("interval ms  p50 %.2f  p99 %.2f  max %.2f  (budget %.2f)\n",
                to_ms(stats.interval_p50), to_ms(stats.interval_p99), to_ms(stats.interval_max),
                to_ms(budget));

    // The brief's budget: sustained rate with p99 frame time within 40 ms.
    constexpr double kP99BudgetMs = 40.0;
    const bool no_drops = stats.dropped == 0;
    const bool within_budget = to_ms(stats.interval_p99) <= kP99BudgetMs;

    std::printf("\nno dropped frames: %s\n", no_drops ? "PASS" : "FAIL");
    std::printf("p99 <= %.0f ms:      %s\n", kP99BudgetMs, within_budget ? "PASS" : "FAIL");

    return (no_drops && within_budget) ? 0 : 1;
}
