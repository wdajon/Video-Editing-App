#include "rf/timeline/composition.hpp"

#include <algorithm>
#include <limits>

namespace rf::timeline {

Result<std::vector<Layer>> layers_at(const Document& document, std::int64_t frame) {
    if (frame < 0) {
        return Error{Errc::invalid_argument,
                     "frame " + std::to_string(frame) + " is before the start of the sequence"};
    }

    const Ticks per_frame = document.ticks_per_frame();
    // Overflow rather than a wrong picture: a frame index large enough to wrap
    // would otherwise silently resolve to somewhere near zero and show the wrong
    // part of the timeline.
    if (frame > std::numeric_limits<Ticks>::max() / per_frame) {
        return Error{Errc::invalid_argument,
                     "frame " + std::to_string(frame) + " is not representable in ticks"};
    }
    const Ticks tick = frame * per_frame;

    std::vector<Layer> layers;
    for (const Track& track : document.tracks()) {
        if (track.kind != TrackKind::video) {
            continue;
        }
        for (const Clip& clip : track.clips) {
            // Half-open: a clip ending at tick T does not cover T, which is what
            // makes butt-joined clips show exactly one picture at the join
            // rather than two or none.
            if (tick < clip.start || tick >= clip.start + clip.duration) {
                continue;
            }
            if (!clip.enabled) {
                break;
            }
            Layer layer;
            layer.clip = clip.id;
            layer.track = track.id;
            layer.source = clip.source;
            layer.source_frame = (clip.source_in + (tick - clip.start)) / per_frame;
            layers.push_back(std::move(layer));
            // Clips on a track never overlap, so the first hit is the only one.
            break;
        }
    }
    return layers;
}

std::int64_t sequence_end_frame(const Document& document) {
    Ticks end = 0;
    for (const Track& track : document.tracks()) {
        for (const Clip& clip : track.clips) {
            end = std::max(end, clip.start + clip.duration);
        }
    }
    return end / document.ticks_per_frame();
}

}  // namespace rf::timeline
