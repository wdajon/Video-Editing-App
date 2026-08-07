#include "rf/app/demo_timeline.hpp"

#include <string>
#include <vector>

namespace rf::app {
namespace {

using timeline::ClipId;
using timeline::Ticks;
using timeline::TrackId;
using timeline::TrackKind;

constexpr int kClips = 4;

}  // namespace

Result<void> build_demo_timeline(timeline::Document& document) {
    if (!document.tracks().empty()) {
        return Error{Errc::already_exists,
                     "the demo timeline needs an empty document; this one already has " +
                         std::to_string(document.tracks().size()) + " tracks"};
    }

    Result<TrackId> video = document.add_track(TrackKind::video, "V1");
    if (!video) {
        return video.error();
    }
    Result<TrackId> audio = document.add_track(TrackKind::audio, "A1");
    if (!audio) {
        return audio.error();
    }

    // Two seconds each, cut from the middle of a six-second source, so every
    // clip has two seconds of handle at both ends and no trim runs out of media
    // in the first few keystrokes.
    const Ticks frame = document.ticks_per_frame();
    const Ticks length = 60 * frame;
    const Ticks handle = 60 * frame;
    const Ticks source_length = handle + length + handle;

    for (int i = 0; i < kClips; ++i) {
        const std::string name = "demo_" + std::to_string(i + 1);
        const Ticks start = static_cast<Ticks>(i) * length;

        Result<ClipId> picture = document.add_clip(video.value(), name + ".mov", handle, start,
                                                   length, source_length);
        if (!picture) {
            return picture.error();
        }
        Result<ClipId> sound = document.add_clip(audio.value(), name + ".wav", handle, start,
                                                 length, source_length);
        if (!sound) {
            return sound.error();
        }

        // Linked, so pressing a trim key on the picture trims the sound with it
        // -- which is the part of the model a person can only check by watching
        // both move together.
        if (Result<timeline::LinkId> linked =
                document.link_clips({picture.value(), sound.value()});
            !linked) {
            return linked.error();
        }
    }

    return ok();
}

}  // namespace rf::app
