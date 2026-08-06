// The timeline document: tracks, clips, and the state that must survive undo.
//
// Every position and duration here is an integer count of `time_base` ticks.
// See docs/adr/005-timeline-model-and-undo.md for why a single document-wide
// base beats per-value rationals or seconds-as-double.

#ifndef RF_TIMELINE_DOCUMENT_HPP
#define RF_TIMELINE_DOCUMENT_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "rf/core/result.hpp"
#include "rf/media/rational.hpp"
#include "rf/timeline/ids.hpp"

namespace rf::timeline {

using Ticks = std::int64_t;

enum class TrackKind : std::uint8_t {
    video = 0,
    audio = 1,
};

[[nodiscard]] std::string_view to_string(TrackKind kind) noexcept;

/// One clip on a track.
///
/// `source_in` is an offset into the source media, in document ticks. `start`
/// is where the clip begins on the timeline. Both are exact integers; neither
/// is derived from the other.
///
/// `source_duration` is how much media the source actually has. It is what makes
/// a trim limit computable, and the document enforces
/// `0 <= source_in && source_in + duration <= source_duration` on every
/// mutation -- so a clip referencing frames that cannot be decoded is not merely
/// unlikely, it is unrepresentable. See docs/adr/009-trim-model.md.
struct Clip {
    ClipId id;
    std::string source;         ///< Media identifier. A path today; a media-pool key later.
    Ticks source_in = 0;        ///< First frame used from the source.
    Ticks source_duration = 0;  ///< Usable extent of the source, from source tick 0.
    Ticks start = 0;            ///< Position on the timeline.
    Ticks duration = 0;         ///< Always > 0 for a clip that exists.
    bool enabled = true;

    friend bool operator==(const Clip&, const Clip&) = default;
};

/// A track holds clips ordered by `start`, and they never overlap.
///
/// The ordering and non-overlap are invariants the document enforces on every
/// mutation, not conventions callers are trusted to maintain: a timeline with
/// two clips occupying the same instant has no defined render result.
struct Track {
    TrackId id;
    TrackKind kind = TrackKind::video;
    std::string name;
    bool muted = false;
    bool locked = false;
    std::vector<Clip> clips;

    friend bool operator==(const Track&, const Track&) = default;
};

/// The whole edit.
class Document {
public:
    /// Creates an empty document with the given tick base, e.g. 1/90000.
    /// Fails on a zero base, which would make every timestamp meaningless.
    [[nodiscard]] static Result<Document> create(const media::Rational& time_base);

    [[nodiscard]] const media::Rational& time_base() const noexcept { return time_base_; }
    [[nodiscard]] const std::vector<Track>& tracks() const noexcept { return tracks_; }

    /// Value of the id counter. Part of the document's state, and serialised,
    /// because restoring a document must restore which ids have been spent --
    /// otherwise a redo could reissue a live id.
    [[nodiscard]] std::uint64_t next_id() const noexcept { return next_id_; }

    [[nodiscard]] const Track* find_track(TrackId id) const noexcept;
    [[nodiscard]] const Clip* find_clip(ClipId id) const noexcept;

    /// Track containing `id`, or nullptr.
    [[nodiscard]] const Track* track_of_clip(ClipId id) const noexcept;

    [[nodiscard]] std::size_t clip_count() const noexcept;

    // --- mutation ------------------------------------------------------------
    // These are the primitives commands are built from. They are deliberately
    // narrow and total: each validates its preconditions and reports an error
    // rather than leaving the document half-changed.

    /// Appends a track and returns its new id.
    [[nodiscard]] Result<TrackId> add_track(TrackKind kind, std::string name);

    /// Inserts a track at `index`, reusing `id`. Used by undo, which must
    /// restore both the identity and the position of a removed track.
    [[nodiscard]] Result<void> insert_track_at(std::size_t index, TrackId id, TrackKind kind,
                                               std::string name, bool muted, bool locked,
                                               std::vector<Clip> clips);

    /// Removes a track and hands back everything needed to put it back exactly.
    [[nodiscard]] Result<Track> remove_track(TrackId id);

    /// Places a clip on a track. Fails if it would overlap an existing clip, or
    /// if the requested span is not inside the source's available media.
    ///
    /// `source_duration` is trailing and required on purpose: a default would be
    /// a guess about media only the caller has probed, and a leading position
    /// would let an existing call site compile with its arguments shifted.
    [[nodiscard]] Result<ClipId> add_clip(TrackId track, std::string source, Ticks source_in,
                                          Ticks start, Ticks duration, Ticks source_duration);

    /// Inserts a clip with a known id, for undo.
    [[nodiscard]] Result<void> insert_clip(TrackId track, const Clip& clip);

    /// Removes a clip and returns it, so the inverse can restore it exactly.
    [[nodiscard]] Result<Clip> remove_clip(ClipId id);

    /// Moves a clip to a new start, possibly onto a different track.
    [[nodiscard]] Result<void> move_clip(ClipId id, TrackId to_track, Ticks new_start);

    /// Changes a clip's source-in and duration together, which is what a trim
    /// is. Fails if the result would overlap a neighbour, be non-positive, or
    /// reach past the end of the clip's source media.
    [[nodiscard]] Result<void> set_clip_bounds(ClipId id, Ticks source_in, Ticks start,
                                               Ticks duration);

    /// Replaces every clip on `track` in one validated step.
    ///
    /// The primitive the trim commands are built from: a ripple, roll or slide
    /// moves several clips at once, and applying such an edit one clip at a time
    /// either overlaps transiently or leaves a half-trimmed track behind when a
    /// later step fails. Here the whole vector is checked first and installed
    /// only if all of it is legal.
    ///
    /// `clips` must carry exactly the same set of ids the track holds now. This
    /// is a rearrangement, not a creation, so the id counter is not involved and
    /// cannot drift -- see docs/adr/009-trim-model.md.
    [[nodiscard]] Result<void> replace_track_clips(TrackId track, std::vector<Clip> clips);

    [[nodiscard]] Result<void> set_track_muted(TrackId id, bool muted);
    [[nodiscard]] Result<void> set_track_locked(TrackId id, bool locked);
    [[nodiscard]] Result<void> set_clip_enabled(ClipId id, bool enabled);

    /// Restores the id counter. Only undo needs this: a command that issued an
    /// id must give it back on inversion, or the counter drifts and two
    /// equivalent histories stop producing identical bytes.
    void rewind_next_id(std::uint64_t value) noexcept;

    friend bool operator==(const Document&, const Document&) = default;

private:
    [[nodiscard]] Track* mutable_track(TrackId id) noexcept;
    [[nodiscard]] Clip* mutable_clip(ClipId id) noexcept;
    [[nodiscard]] Result<void> check_no_overlap(const Track& track, Ticks start, Ticks duration,
                                                ClipId ignoring) const;
    /// The span invariants every clip must satisfy, wherever it came from.
    [[nodiscard]] static Result<void> check_span(Ticks source_in, Ticks start, Ticks duration,
                                                 Ticks source_duration);
    static void sort_clips(Track& track);

    media::Rational time_base_{1, 90000};
    std::vector<Track> tracks_;
    std::uint64_t next_id_ = 1;  // 0 is the null id and is never issued.
};

}  // namespace rf::timeline

#endif  // RF_TIMELINE_DOCUMENT_HPP
