#include "rf/timeline/document.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace rf::timeline {
namespace {

/// Two spans [a_start, a_start+a_len) and [b_start, b_start+b_len) overlap when
/// each begins before the other ends. Touching end-to-start is not an overlap:
/// butt-joined clips are the normal case in an edit, not an error.
constexpr bool spans_overlap(Ticks a_start, Ticks a_len, Ticks b_start, Ticks b_len) noexcept {
    return a_start < b_start + b_len && b_start < a_start + a_len;
}

}  // namespace

std::string_view to_string(TrackKind kind) noexcept {
    switch (kind) {
        case TrackKind::video: return "video";
        case TrackKind::audio: return "audio";
    }
    return "video";
}

Result<Document> Document::create(const media::Rational& time_base) {
    if (time_base.is_zero()) {
        return Error{Errc::invalid_argument,
                     "document time base cannot be zero; every timestamp would be meaningless"};
    }
    if (time_base.numerator() < 0) {
        return Error{Errc::invalid_argument, "document time base cannot be negative"};
    }
    Document document;
    document.time_base_ = time_base;
    return document;
}

const Track* Document::find_track(TrackId id) const noexcept {
    for (const Track& track : tracks_) {
        if (track.id == id) {
            return &track;
        }
    }
    return nullptr;
}

Track* Document::mutable_track(TrackId id) noexcept {
    for (Track& track : tracks_) {
        if (track.id == id) {
            return &track;
        }
    }
    return nullptr;
}

const Clip* Document::find_clip(ClipId id) const noexcept {
    for (const Track& track : tracks_) {
        for (const Clip& clip : track.clips) {
            if (clip.id == id) {
                return &clip;
            }
        }
    }
    return nullptr;
}

Clip* Document::mutable_clip(ClipId id) noexcept {
    for (Track& track : tracks_) {
        for (Clip& clip : track.clips) {
            if (clip.id == id) {
                return &clip;
            }
        }
    }
    return nullptr;
}

const Track* Document::track_of_clip(ClipId id) const noexcept {
    for (const Track& track : tracks_) {
        for (const Clip& clip : track.clips) {
            if (clip.id == id) {
                return &track;
            }
        }
    }
    return nullptr;
}

std::size_t Document::clip_count() const noexcept {
    std::size_t total = 0;
    for (const Track& track : tracks_) {
        total += track.clips.size();
    }
    return total;
}

void Document::sort_clips(Track& track) {
    // Ordered by start, then by id so the order is total. Without the id
    // tiebreak, two zero-length-gap arrangements could serialise differently
    // depending on insertion history, and byte-identity would be unreliable.
    std::stable_sort(track.clips.begin(), track.clips.end(), [](const Clip& a, const Clip& b) {
        if (a.start != b.start) {
            return a.start < b.start;
        }
        return a.id < b.id;
    });
}

Result<void> Document::check_no_overlap(const Track& track, Ticks start, Ticks duration,
                                        ClipId ignoring) const {
    for (const Clip& clip : track.clips) {
        if (clip.id == ignoring) {
            continue;
        }
        if (spans_overlap(start, duration, clip.start, clip.duration)) {
            return Error{Errc::invalid_argument,
                         "clip would overlap " + to_string(clip.id) + " on " + to_string(track.id)};
        }
    }
    return ok();
}

Result<void> Document::check_span(Ticks source_in, Ticks start, Ticks duration,
                                  Ticks source_duration) {
    if (duration <= 0) {
        return Error{Errc::invalid_argument,
                     "clip duration must be positive, got " + std::to_string(duration)};
    }
    if (start < 0) {
        return Error{Errc::invalid_argument,
                     "clip start must not be negative, got " + std::to_string(start)};
    }
    if (source_in < 0) {
        return Error{Errc::invalid_argument,
                     "clip source_in must not be negative, got " + std::to_string(source_in)};
    }
    // A clip whose end is not representable would make every downstream
    // arithmetic step -- a ripple's shift, a slide's limit -- an overflow
    // waiting to happen. Refusing it here is what lets the trim ranges be
    // computed with plain signed arithmetic and no saturation logic.
    if (start > std::numeric_limits<Ticks>::max() - duration) {
        return Error{Errc::invalid_argument, "clip start " + std::to_string(start) + " plus " +
                                                 std::to_string(duration) +
                                                 " ticks is not representable"};
    }
    if (source_duration < 0) {
        return Error{Errc::invalid_argument, "clip source_duration must not be negative, got " +
                                                 std::to_string(source_duration)};
    }
    // Written as a subtraction so a source_in near INT64_MAX cannot overflow the
    // addition and wrap into a value that passes. Both operands are known
    // non-negative by the checks above, so the subtraction itself cannot
    // overflow either.
    if (source_duration - source_in < duration) {
        return Error{Errc::invalid_argument,
                     "clip needs " + std::to_string(duration) + " ticks from source offset " +
                         std::to_string(source_in) + ", but the source has only " +
                         std::to_string(source_duration) + " ticks"};
    }
    return ok();
}

Result<TrackId> Document::add_track(TrackKind kind, std::string name) {
    Track track;
    track.id = TrackId{next_id_++};
    track.kind = kind;
    track.name = std::move(name);
    const TrackId id = track.id;
    tracks_.push_back(std::move(track));
    return id;
}

Result<void> Document::insert_track_at(std::size_t index, Track track) {
    if (index > tracks_.size()) {
        return Error{Errc::invalid_argument, "track index out of range"};
    }
    if (find_track(track.id) != nullptr) {
        return Error{Errc::already_exists, to_string(track.id) + " is already present"};
    }
    for (const Clip& clip : track.clips) {
        if (find_clip(clip.id) != nullptr) {
            return Error{Errc::already_exists, to_string(clip.id) + " is already present"};
        }
    }

    sort_clips(track);
    tracks_.insert(tracks_.begin() + static_cast<std::ptrdiff_t>(index), std::move(track));
    return ok();
}

Result<Track> Document::remove_track(TrackId id) {
    for (std::size_t i = 0; i < tracks_.size(); ++i) {
        if (tracks_[i].id == id) {
            Track removed = std::move(tracks_[i]);
            tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(i));
            return removed;
        }
    }
    return Error{Errc::not_found, to_string(id) + " does not exist"};
}

Result<ClipId> Document::add_clip(TrackId track_id, std::string source, Ticks source_in,
                                  Ticks start, Ticks duration, Ticks source_duration) {
    if (Result<void> span = check_span(source_in, start, duration, source_duration); !span) {
        return span.error();
    }

    Track* track = mutable_track(track_id);
    if (track == nullptr) {
        return Error{Errc::not_found, to_string(track_id) + " does not exist"};
    }
    if (Result<void> clear = check_no_overlap(*track, start, duration, ClipId{}); !clear) {
        return clear.error();
    }

    Clip clip;
    clip.id = ClipId{next_id_++};
    clip.source = std::move(source);
    clip.source_in = source_in;
    clip.source_duration = source_duration;
    clip.start = start;
    clip.duration = duration;
    const ClipId id = clip.id;
    track->clips.push_back(std::move(clip));
    sort_clips(*track);
    return id;
}

Result<void> Document::insert_clip(TrackId track_id, const Clip& clip) {
    if (Result<void> span = check_span(clip.source_in, clip.start, clip.duration,
                                       clip.source_duration);
        !span) {
        return span.error();
    }
    if (!clip.id.is_valid()) {
        return Error{Errc::invalid_argument, "cannot insert a clip with the null id"};
    }
    if (find_clip(clip.id) != nullptr) {
        return Error{Errc::already_exists, to_string(clip.id) + " is already present"};
    }

    Track* track = mutable_track(track_id);
    if (track == nullptr) {
        return Error{Errc::not_found, to_string(track_id) + " does not exist"};
    }
    if (Result<void> clear = check_no_overlap(*track, clip.start, clip.duration, ClipId{}); !clear) {
        return clear.error();
    }

    track->clips.push_back(clip);
    sort_clips(*track);
    return ok();
}

Result<Clip> Document::remove_clip(ClipId id) {
    for (Track& track : tracks_) {
        for (std::size_t i = 0; i < track.clips.size(); ++i) {
            if (track.clips[i].id == id) {
                Clip removed = std::move(track.clips[i]);
                track.clips.erase(track.clips.begin() + static_cast<std::ptrdiff_t>(i));
                return removed;
            }
        }
    }
    return Error{Errc::not_found, to_string(id) + " does not exist"};
}

Result<void> Document::move_clip(ClipId id, TrackId to_track, Ticks new_start) {
    if (new_start < 0) {
        return Error{Errc::invalid_argument, "clip start must not be negative"};
    }
    const Clip* existing = find_clip(id);
    if (existing == nullptr) {
        return Error{Errc::not_found, to_string(id) + " does not exist"};
    }
    Track* destination = mutable_track(to_track);
    if (destination == nullptr) {
        return Error{Errc::not_found, to_string(to_track) + " does not exist"};
    }
    if (Result<void> clear = check_no_overlap(*destination, new_start, existing->duration, id);
        !clear) {
        return clear.error();
    }

    // Copy before removing: the pointer above dangles the moment the source
    // track's vector is modified.
    Clip moved = *existing;
    moved.start = new_start;
    Result<Clip> removed = remove_clip(id);
    if (!removed) {
        return removed.error();
    }
    destination->clips.push_back(std::move(moved));
    sort_clips(*destination);
    return ok();
}

Result<void> Document::set_clip_bounds(ClipId id, Ticks source_in, Ticks start, Ticks duration) {
    const Track* owner = track_of_clip(id);
    if (owner == nullptr) {
        return Error{Errc::not_found, to_string(id) + " does not exist"};
    }
    if (Result<void> span = check_span(source_in, start, duration, find_clip(id)->source_duration);
        !span) {
        return span.error();
    }
    if (Result<void> clear = check_no_overlap(*owner, start, duration, id); !clear) {
        return clear.error();
    }

    Clip* clip = mutable_clip(id);
    clip->source_in = source_in;
    clip->start = start;
    clip->duration = duration;
    sort_clips(*mutable_track(owner->id));
    return ok();
}

Result<void> Document::replace_track_clips(TrackId id, std::vector<Clip> clips) {
    std::vector<TrackClips> single;
    single.push_back(TrackClips{id, std::move(clips)});
    return replace_clips(std::move(single));
}

Result<void> Document::replace_clips(std::vector<TrackClips> rewrites) {
    // Validate everything into a staging vector first. Installing as we go would
    // leave a ripple across four tracks having changed three of them when the
    // fourth turned out to be illegal, and nothing would record how far it got.
    std::vector<std::vector<Clip>> staged;
    staged.reserve(rewrites.size());

    for (TrackClips& rewrite : rewrites) {
        Result<std::vector<Clip>> checked = validate_track_clips(rewrite.track,
                                                                 std::move(rewrite.clips));
        if (!checked) {
            return checked.error();
        }
        staged.push_back(std::move(checked).value());
    }

    for (std::size_t i = 0; i < rewrites.size(); ++i) {
        mutable_track(rewrites[i].track)->clips = std::move(staged[i]);
    }
    return ok();
}

Result<std::vector<Clip>> Document::validate_track_clips(TrackId id, std::vector<Clip> clips) const {
    const Track* track = find_track(id);
    if (track == nullptr) {
        return Error{Errc::not_found, to_string(id) + " does not exist"};
    }
    if (clips.size() != track->clips.size()) {
        return Error{Errc::invalid_argument,
                     "replacement for " + to_string(id) + " has " + std::to_string(clips.size()) +
                         " clips, the track has " + std::to_string(track->clips.size())};
    }

    for (const Clip& clip : clips) {
        if (Result<void> span = check_span(clip.source_in, clip.start, clip.duration,
                                           clip.source_duration);
            !span) {
            return span.error().with_context(to_string(clip.id));
        }
        // The id set has to match exactly. A replacement that introduces an id
        // would spend one the counter never issued, and one that drops an id
        // would delete a clip through a primitive whose inverse assumes nothing
        // was destroyed. Both would break ADR 005's byte-identity guarantee in a
        // way no overlap check would notice.
        const auto same_id = [&clip](const Clip& existing) { return existing.id == clip.id; };
        if (std::none_of(track->clips.begin(), track->clips.end(), same_id)) {
            return Error{Errc::invalid_argument,
                         to_string(clip.id) + " is not on " + to_string(id)};
        }
        if (std::count_if(clips.begin(), clips.end(), same_id) != 1) {
            return Error{Errc::invalid_argument,
                         to_string(clip.id) + " appears more than once in the replacement"};
        }
    }

    // Sizes match, every incoming id is on the track, and no incoming id repeats,
    // so the two sets are equal without a second sweep.
    Track candidate;
    candidate.clips = std::move(clips);
    sort_clips(candidate);
    for (std::size_t i = 1; i < candidate.clips.size(); ++i) {
        const Clip& previous = candidate.clips[i - 1];
        const Clip& current = candidate.clips[i];
        if (current.start < previous.start + previous.duration) {
            return Error{Errc::invalid_argument, to_string(current.id) + " would overlap " +
                                                     to_string(previous.id) + " on " +
                                                     to_string(id)};
        }
    }

    return std::move(candidate.clips);
}

Result<void> Document::set_track_muted(TrackId id, bool muted) {
    Track* track = mutable_track(id);
    if (track == nullptr) {
        return Error{Errc::not_found, to_string(id) + " does not exist"};
    }
    track->muted = muted;
    return ok();
}

Result<void> Document::set_track_locked(TrackId id, bool locked) {
    Track* track = mutable_track(id);
    if (track == nullptr) {
        return Error{Errc::not_found, to_string(id) + " does not exist"};
    }
    track->locked = locked;
    return ok();
}

Result<void> Document::set_track_sync_locked(TrackId id, bool sync_locked) {
    Track* track = mutable_track(id);
    if (track == nullptr) {
        return Error{Errc::not_found, to_string(id) + " does not exist"};
    }
    track->sync_locked = sync_locked;
    return ok();
}

Result<void> Document::set_clip_enabled(ClipId id, bool enabled) {
    Clip* clip = mutable_clip(id);
    if (clip == nullptr) {
        return Error{Errc::not_found, to_string(id) + " does not exist"};
    }
    clip->enabled = enabled;
    return ok();
}

void Document::rewind_next_id(std::uint64_t value) noexcept {
    next_id_ = value;
}

}  // namespace rf::timeline
