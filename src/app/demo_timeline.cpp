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

std::string demo_media_relative_path() {
    return "tests/fixtures/media/bars_320x240_30fps_h264_aac.mp4";
}

Result<void> build_demo_timeline(timeline::Document& document,
                                 const std::string& video_source) {
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

    // Twenty frames each, cut from the middle of a sixty-frame source: a third
    // of the media used, a third of handle either side.
    //
    // Sixty frames because that is exactly what the repository's fixture holds
    // (two seconds at 30 fps). A demo that claimed more would describe media the
    // file does not have, and rendering it would fail on a frame that was never
    // there -- which is the same lie as a placeholder frame, told earlier.
    const Ticks frame = document.ticks_per_frame();
    const Ticks length = 20 * frame;
    const Ticks handle = 20 * frame;
    const Ticks source_length = handle + length + handle;

    // A gap before the last pair only.
    //
    // The two halves of the trim set want opposite things: roll and slide need a
    // butt-joined neighbour, and nudge needs free space. No single clip can
    // offer both. So the first three are butt-joined -- that is where ripple,
    // roll, slip and slide all work, and it is the clip the demo selects -- and
    // the last one sits past a gap, which is where a nudge has somewhere to go.
    const Ticks gap = 10 * frame;

    for (int i = 0; i < kClips; ++i) {
        const std::string name = "demo_" + std::to_string(i + 1);
        const Ticks start = static_cast<Ticks>(i) * length + (i >= kClips - 1 ? gap : 0);

        // Every picture clip points at the same file when one is supplied: the
        // demo is about the edit, and four copies of one source still exercise
        // every trim while keeping the decoder cache honest about reuse.
        const std::string picture_source = video_source.empty() ? name + ".mov" : video_source;
        Result<ClipId> picture = document.add_clip(video.value(), picture_source, handle, start,
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
