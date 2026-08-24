// How long does a timeline frame take, and where does the time go?
//
// The Program panel renders through `SequenceRenderer`, which reads pixels back
// from the GPU for every frame. M3 measured that shape at p50 49.9 ms for
// 1080x1920 with three layers (D13) and replaced it in the playback path -- but
// that was a different workload, and the project's own rule is that every
// performance guess made here has been wrong by at least 4x in one direction or
// the other. So: measure before deciding whether the monitor needs the
// GPU-resident path, and measure again after.
//
// Reports the whole per-frame cost. Splitting decode from composite from
// readback would need instrumentation inside the renderer; the total is what
// decides whether playback is possible at all, so it is what this reports.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "rf/gpu/compositor.hpp"
#include "rf/gpu/device.hpp"
#include "rf/gpu/texture.hpp"
#include "rf/media/convert.hpp"
#include "rf/media/decoder.hpp"
#include "rf/gpu/instance.hpp"
#include "rf/media/probe.hpp"
#include "rf/render/sequence_renderer.hpp"
#include "rf/timeline/document.hpp"

namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] double milliseconds(Clock::duration span) {
    return std::chrono::duration<double, std::milli>(span).count();
}

[[nodiscard]] double percentile(std::vector<double> samples, double fraction) {
    if (samples.empty()) {
        return 0.0;
    }
    std::sort(samples.begin(), samples.end());
    const auto index = static_cast<std::size_t>(fraction * static_cast<double>(samples.size() - 1));
    return samples[index];
}

int usage() {
    std::fprintf(stderr,
                 "usage: rf_render_bench --source <file> [--frames N] [--layers N] [--readback]\n"
                 "\n"
                 "Builds a timeline of <layers> stacked video tracks all showing\n"
                 "<file>, renders <frames> consecutive frames, and reports the\n"
                 "per-frame cost against the 33.33 ms budget for 30 fps.\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    std::string source;
    int frames = 120;
    int layers = 1;
    bool readback = false;
    bool breakdown = false;

    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        const bool has_value = i + 1 < argc;
        if (flag == "--source" && has_value) {
            source = argv[++i];
        } else if (flag == "--frames" && has_value) {
            frames = std::atoi(argv[++i]);
        } else if (flag == "--layers" && has_value) {
            layers = std::atoi(argv[++i]);
        } else if (flag == "--breakdown") {
            // Times the stages separately, to say where the per-frame
            // cost actually is before anything is optimised for it.
            breakdown = true;
        } else if (flag == "--readback") {
            // The path the Program panel used before it presented: pixels come
            // back to the CPU so a QWidget can paint them.
            readback = true;
        } else {
            return usage();
        }
    }
    if (source.empty() || frames <= 0 || layers <= 0) {
        return usage();
    }

    auto info = rf::media::probe_file(source);
    if (!info) {
        std::fprintf(stderr, "%s\n", info.error().to_string().c_str());
        return 1;
    }
    const rf::media::StreamInfo* video = info.value().primary_video();
    if (video == nullptr || !video->video) {
        std::fprintf(stderr, "%s has no video stream\n", source.c_str());
        return 1;
    }
    const int width = video->video->width;
    const int height = video->video->height;

    auto instance = rf::gpu::Instance::create(rf::gpu::Instance::Options{});
    if (!instance) {
        std::fprintf(stderr, "%s\n", instance.error().to_string().c_str());
        return 1;
    }
    auto device = rf::gpu::Device::create_preferred(instance.value());
    if (!device) {
        std::fprintf(stderr, "%s\n", device.error().to_string().c_str());
        return 1;
    }
    std::printf("device:  %s\n", device.value().info().name.c_str());
    std::printf("path:    %s\n", readback ? "readback (CPU pixels out)" : "device-resident");
    std::printf("scene:   %dx%d, %d layer(s), %d frames\n", width, height, layers, frames);

    // 1/90000 at 30 fps: one frame is exactly 3000 ticks.
    auto document = rf::timeline::Document::create(rf::media::Rational{1, 90000},
                                                   rf::media::Rational{30, 1});
    if (!document) {
        std::fprintf(stderr, "%s\n", document.error().to_string().c_str());
        return 1;
    }
    const rf::timeline::Ticks per_frame = document.value().ticks_per_frame();
    const rf::timeline::Ticks span = static_cast<rf::timeline::Ticks>(frames) * per_frame;

    for (int i = 0; i < layers; ++i) {
        auto track = document.value().add_track(rf::timeline::TrackKind::video,
                                                "V" + std::to_string(i + 1));
        if (!track) {
            std::fprintf(stderr, "%s\n", track.error().to_string().c_str());
            return 1;
        }
        // Every layer shows the same file, which is the honest worst case for
        // the decoder cache: one decoder, seeked and read `layers` times per
        // frame. Separate sources would each get their own and decode in
        // parallel-ish; this does not, so the number is not optimistic.
        auto clip = document.value().add_clip(track.value(), source, 0, 0, span, span);
        if (!clip) {
            std::fprintf(stderr, "%s\n", clip.error().to_string().c_str());
            return 1;
        }
    }

    if (breakdown) {
        // Walks the same stages the renderer does, timing each. Not a second
        // implementation of rendering -- it is deliberately the simplest
        // possible sequence, so the numbers are attributable rather than
        // entangled with the renderer's bookkeeping.
        auto decoder = rf::media::VideoDecoder::open(source);
        if (!decoder) {
            std::fprintf(stderr, "%s\n", decoder.error().to_string().c_str());
            return 1;
        }
        auto compositor = rf::gpu::Compositor::create(device.value());
        auto target = rf::gpu::Texture::create(device.value(), width, height);
        auto upload = rf::gpu::Texture::create(device.value(), width, height);
        if (!compositor || !target || !upload) {
            std::fprintf(stderr, "could not build the GPU objects\n");
            return 1;
        }

        std::vector<double> seek_ms;
        std::vector<double> decode_ms;
        std::vector<double> convert_ms;
        std::vector<double> gpu_ms;
        for (int frame = 0; frame < frames; ++frame) {
            // The renderer seeks before every frame, because it is asked for a
            // frame index rather than "the next one". This is the only call the
            // earlier breakdown left out, and the numbers did not add up without
            // it -- so it is timed separately rather than assumed cheap.
            const auto tseek = Clock::now();
            if (auto sought = decoder.value().seek_to_frame(frame); !sought) {
                std::fprintf(stderr, "%s\n", sought.error().to_string().c_str());
                return 1;
            }
            const auto t0 = Clock::now();
            auto decoded = decoder.value().next_frame();
            const auto t1 = Clock::now();
            if (!decoded || !decoded.value()) {
                break;
            }
            auto rgba = rf::media::to_rgba8(decoded.value().value());
            const auto t2 = Clock::now();
            if (!rgba) {
                std::fprintf(stderr, "%s\n", rgba.error().to_string().c_str());
                return 1;
            }
            rf::gpu::ImageRgba8 image;
            image.width = rgba.value().width();
            image.height = rgba.value().height();
            image.pixels = rgba.value().pixels();
            if (auto put = upload.value().upload(image); !put) {
                std::fprintf(stderr, "%s\n", put.error().to_string().c_str());
                return 1;
            }
            std::vector<rf::gpu::GpuLayer> stack{{&upload.value(), 1.0F, true}};
            if (auto composed = compositor.value().composite_into(target.value(), stack);
                !composed) {
                std::fprintf(stderr, "%s\n", composed.error().to_string().c_str());
                return 1;
            }
            const auto t3 = Clock::now();

            seek_ms.push_back(milliseconds(t0 - tseek));
            decode_ms.push_back(milliseconds(t1 - t0));
            convert_ms.push_back(milliseconds(t2 - t1));
            gpu_ms.push_back(milliseconds(t3 - t2));
        }

        std::printf("\nper-frame breakdown (p50 ms)\n");
        std::printf("  seek_to_frame    %6.2f\n", percentile(seek_ms, 0.50));
        std::printf("  decode           %6.2f\n", percentile(decode_ms, 0.50));
        std::printf("  yuv->rgba (cpu)  %6.2f\n", percentile(convert_ms, 0.50));
        std::printf("  copy+upload+comp %6.2f\n", percentile(gpu_ms, 0.50));
        return 0;
    }

    auto renderer = rf::render::SequenceRenderer::create(device.value(), width, height);
    if (!renderer) {
        std::fprintf(stderr, "%s\n", renderer.error().to_string().c_str());
        return 1;
    }

    // One frame first, uncounted: it opens the file and builds the pipeline, and
    // charging that to the first sample would flatter every number after it.
    if (auto warm = renderer.value().render_to_texture(document.value(), 0); !warm) {
        std::fprintf(stderr, "%s\n", warm.error().to_string().c_str());
        return 1;
    }

    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(frames));
    const std::int64_t materialised_before = renderer.value().frames_materialised();
    for (int frame = 0; frame < frames; ++frame) {
        const auto started = Clock::now();
        std::string failure;
        if (readback) {
            auto image = renderer.value().render(document.value(), frame);
            if (!image) {
                failure = image.error().to_string();
            }
        } else {
            auto texture = renderer.value().render_to_texture(document.value(), frame);
            if (!texture) {
                failure = texture.error().to_string();
            }
        }
        const auto finished = Clock::now();
        if (!failure.empty()) {
            std::fprintf(stderr, "frame %d: %s\n", frame, failure.c_str());
            return 1;
        }
        samples.push_back(milliseconds(finished - started));
    }
    const std::int64_t materialised =
        renderer.value().frames_materialised() - materialised_before;

    const double p50 = percentile(samples, 0.50);
    const double p99 = percentile(samples, 0.99);
    const double worst = *std::max_element(samples.begin(), samples.end());
    constexpr double kBudget = 1000.0 / 30.0;

    // The number that says whether the cost is the picture or the seeking. One
    // decoded frame per rendered frame per layer is the floor; anything above it
    // is a seek decoding forward from a keyframe.
    std::printf("decoded   %lld frames for %d rendered (%.1f per frame, floor %d)\n",
                static_cast<long long>(materialised), frames,
                static_cast<double>(materialised) / frames, layers);
    std::printf("frame ms  p50 %.2f  p99 %.2f  max %.2f  (budget %.2f)\n", p50, p99, worst,
                kBudget);
    std::printf("sustained 30 fps: %s\n", p99 <= kBudget ? "PASS" : "FAIL");
    return p99 <= kBudget ? 0 : 1;
}
