// rf_seek_check -- verifies frame-accurate seeking against a linear-decode
// oracle, and measures seek latency against the M1 budget.
//
// Exists because the M1 exit gate names a 10-minute 4K file, which is far too
// large to commit as a fixture and far too slow to seek exhaustively inside the
// unit test suite. This runs against any file on disk:
//
//   rf_seek_check <file> [--seeks N] [--seed S] [--budget-ms N]
//
// Pass 1 decodes the whole file, recording one hash per frame -- the reference.
// Pass 2 seeks to randomly chosen frames and checks that what comes back
// matches the reference, timing each seek from request to displayed frame.
//
// Exits non-zero on any mismatch or budget breach, so it is usable as a gate.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "rf/media/decoder.hpp"
#include "rf/media/probe.hpp"
#include "rf/media/video_frame.hpp"

namespace {

struct Options {
    std::string path;
    int seeks = 200;
    unsigned int seed = 20260804u;
    double budget_ms = 150.0;  // Performance budget: random seek in long-GOP 4K.
};

void print_usage() {
    std::fprintf(stderr,
                 "usage: rf_seek_check <file> [--seeks N] [--seed S] [--budget-ms N]\n");
}

bool parse_options(int argc, char** argv, Options& options) {
    if (argc < 2) {
        return false;
    }
    options.path = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string_view flag = argv[i];
        if (i + 1 >= argc) {
            std::fprintf(stderr, "missing value for %.*s\n", static_cast<int>(flag.size()),
                         flag.data());
            return false;
        }
        const std::string value = argv[++i];
        if (flag == "--seeks") {
            options.seeks = std::atoi(value.c_str());
        } else if (flag == "--seed") {
            options.seed = static_cast<unsigned int>(std::strtoul(value.c_str(), nullptr, 10));
        } else if (flag == "--budget-ms") {
            options.budget_ms = std::strtod(value.c_str(), nullptr);
        } else {
            std::fprintf(stderr, "unknown flag: %.*s\n", static_cast<int>(flag.size()),
                         flag.data());
            return false;
        }
    }
    return options.seeks > 0;
}

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(
        std::min<double>(static_cast<double>(values.size()) - 1.0,
                         fraction * static_cast<double>(values.size() - 1)));
    return values[index];
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, options)) {
        print_usage();
        return 2;
    }

    auto info = rf::media::probe_file(options.path);
    if (!info) {
        std::fprintf(stderr, "probe failed: %s\n", info.error().to_string().c_str());
        return 1;
    }
    const auto* video = info.value().primary_video();
    if (video == nullptr || !video->video.has_value()) {
        std::fprintf(stderr, "%s has no video stream\n", options.path.c_str());
        return 1;
    }

    std::printf("file:       %s\n", options.path.c_str());
    std::printf("resolution: %dx%d\n", video->video->width, video->video->height);
    std::printf("frame rate: %s\n",
                video->video->average_frame_rate.has_value()
                    ? video->video->average_frame_rate->to_string().c_str()
                    : "not stated");
    std::printf("time base:  %s\n", video->time_base.to_string().c_str());
    std::fflush(stdout);

    // --- pass 1: the reference ------------------------------------------------
    auto reference_decoder = rf::media::VideoDecoder::open(options.path);
    if (!reference_decoder) {
        std::fprintf(stderr, "open failed: %s\n", reference_decoder.error().to_string().c_str());
        return 1;
    }

    std::vector<std::uint64_t> reference;
    const auto decode_start = std::chrono::steady_clock::now();
    for (;;) {
        auto frame = reference_decoder.value().next_frame();
        if (!frame) {
            std::fprintf(stderr, "decode failed at frame %zu: %s\n", reference.size(),
                         frame.error().to_string().c_str());
            return 1;
        }
        if (!frame.value().has_value()) {
            break;
        }
        reference.push_back(rf::media::frame_hash(frame.value().value()));
    }
    const auto decode_elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - decode_start).count();

    if (reference.empty()) {
        std::fprintf(stderr, "no frames decoded\n");
        return 1;
    }

    // A reference in which many frames share a hash would make the comparison
    // below pass without proving anything.
    std::vector<std::uint64_t> sorted = reference;
    std::sort(sorted.begin(), sorted.end());
    const std::size_t distinct =
        static_cast<std::size_t>(std::unique(sorted.begin(), sorted.end()) - sorted.begin());

    std::printf("\nlinear decode: %zu frames in %.1f s (%.1f fps), %zu distinct hashes\n",
                reference.size(), decode_elapsed,
                static_cast<double>(reference.size()) / decode_elapsed, distinct);
    if (distinct < reference.size()) {
        std::printf("  note: %zu duplicate hashes; seek checks on those frames prove less\n",
                    reference.size() - distinct);
    }
    std::fflush(stdout);

    // --- pass 2: seeks --------------------------------------------------------
    auto decoder = rf::media::VideoDecoder::open(options.path);
    if (!decoder) {
        std::fprintf(stderr, "reopen failed: %s\n", decoder.error().to_string().c_str());
        return 1;
    }

    std::mt19937 generator{options.seed};
    std::uniform_int_distribution<std::int64_t> pick{0, static_cast<std::int64_t>(reference.size()) - 1};

    std::vector<double> latencies;
    latencies.reserve(static_cast<std::size_t>(options.seeks));
    int mismatches = 0;

    for (int i = 0; i < options.seeks; ++i) {
        const std::int64_t index = pick(generator);

        // Timed from the request to a displayable frame, which is the number a
        // user experiences -- not just the container-level seek.
        const auto start = std::chrono::steady_clock::now();
        auto sought = decoder.value().seek_to_frame(index);
        if (!sought) {
            std::fprintf(stderr, "seek to %lld failed: %s\n", static_cast<long long>(index),
                         sought.error().to_string().c_str());
            return 1;
        }
        auto frame = decoder.value().next_frame();
        const auto elapsed = std::chrono::steady_clock::now() - start;

        if (!frame) {
            std::fprintf(stderr, "decode after seek to %lld failed: %s\n",
                         static_cast<long long>(index), frame.error().to_string().c_str());
            return 1;
        }
        if (!frame.value().has_value()) {
            std::fprintf(stderr, "seek to %lld produced no frame\n", static_cast<long long>(index));
            return 1;
        }

        latencies.push_back(std::chrono::duration<double, std::milli>(elapsed).count());

        const rf::media::VideoFrame& decoded = frame.value().value();
        if (decoded.frame_index.value_or(-1) != index) {
            std::fprintf(stderr, "frame index mismatch: asked %lld, got %lld\n",
                         static_cast<long long>(index),
                         static_cast<long long>(decoded.frame_index.value_or(-1)));
            ++mismatches;
            continue;
        }
        if (rf::media::frame_hash(decoded) != reference[static_cast<std::size_t>(index)]) {
            std::fprintf(stderr, "HASH MISMATCH at frame %lld\n", static_cast<long long>(index));
            ++mismatches;
        }
    }

    const double p50 = percentile(latencies, 0.50);
    const double p95 = percentile(latencies, 0.95);
    const double p99 = percentile(latencies, 0.99);
    const double worst = *std::max_element(latencies.begin(), latencies.end());
    const double mean =
        std::accumulate(latencies.begin(), latencies.end(), 0.0) / static_cast<double>(latencies.size());

    std::printf("\nseeks: %d  mismatches: %d\n", options.seeks, mismatches);
    std::printf("latency ms  mean %.1f  p50 %.1f  p95 %.1f  p99 %.1f  max %.1f  (budget %.0f)\n",
                mean, p50, p95, p99, worst, options.budget_ms);

    const bool budget_met = p99 <= options.budget_ms;
    std::printf("\naccuracy: %s\n", mismatches == 0 ? "PASS" : "FAIL");
    std::printf("budget:   %s\n", budget_met ? "PASS" : "FAIL");

    return (mismatches == 0 && budget_met) ? 0 : 1;
}
