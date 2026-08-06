// Random trims against a random track, checked against the properties that
// define each operation rather than against expected numbers.
//
// The hand-written cases in trim_test.cpp assert exact results on one fixture.
// This asserts the things that must hold for *every* input: that a trim never
// leaves an illegal document, that undoing everything comes back byte-identical,
// and that each operation preserves what its definition says it preserves. Those
// invariants are what a wrong sign or an off-by-one in a limit shows up as.

#include "rf/timeline/trim.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "rf/timeline/command.hpp"
#include "rf/timeline/document.hpp"
#include "rf/timeline/serialise.hpp"

namespace {

using rf::media::Rational;
using rf::timeline::Clip;
using rf::timeline::ClipId;
using rf::timeline::CommandStack;
using rf::timeline::Document;
using rf::timeline::Ticks;
using rf::timeline::Track;
using rf::timeline::TrackId;
using rf::timeline::TrackKind;
using rf::timeline::TrimKind;
using rf::timeline::make_trim;
using rf::timeline::serialise;

constexpr TrimKind kKinds[] = {TrimKind::ripple_in, TrimKind::ripple_out, TrimKind::roll,
                               TrimKind::slip, TrimKind::slide};

/// Fills one track with butt-joined and gapped clips, each using a random window
/// of a source longer than the window, so trims have room in both directions.
void fill_track(Document& document, TrackId track, std::mt19937_64& random) {
    Ticks cursor = static_cast<Ticks>(random() % 200);
    const int clips = 2 + static_cast<int>(random() % 8);
    for (int i = 0; i < clips; ++i) {
        const Ticks duration = static_cast<Ticks>(10 + (random() % 400));
        const Ticks head = static_cast<Ticks>(random() % 300);
        const Ticks tail = static_cast<Ticks>(random() % 300);
        const auto added = document.add_clip(track, "m" + std::to_string(i) + ".mp4", head, cursor,
                                             duration, head + duration + tail);
        EXPECT_TRUE(added.has_value()) << added.error().to_string();
        cursor += duration;
        // Half the joins are butt-joined, which is where roll and slide have
        // anything to do; the rest leave a gap, which is the other branch.
        if (random() % 2 == 0) {
            cursor += static_cast<Ticks>(1 + (random() % 200));
        }
    }
}

/// Two or three tracks, independently laid out, with sync lock set at random.
///
/// The second track matters: with one track a ripple has nothing to keep in
/// sync, and every sync-lock path in ADR 010 -- the range narrowing, the
/// straddling refusal, the multi-track undo record -- goes unexercised.
Document random_document(std::mt19937_64& random) {
    Document document = Document::create(Rational{1, 90000}, Rational{30, 1}).value();
    const int tracks = 2 + static_cast<int>(random() % 2);
    for (int i = 0; i < tracks; ++i) {
        const TrackKind kind = i == 0 ? TrackKind::video : TrackKind::audio;
        const TrackId track = document.add_track(kind, "T" + std::to_string(i)).value();
        fill_track(document, track, random);
        if (random() % 4 == 0) {
            EXPECT_TRUE(document.set_track_sync_locked(track, false).has_value());
        }
    }

    // Independently laid out tracks essentially never produce two clips with the
    // same span, so a linked pair has to be built on purpose: take a clip from
    // the first track and lay its counterpart on another track at the same span.
    // Whichever attempts collide with existing clips are simply skipped.
    const TrackId first = document.tracks().front().id;
    const std::size_t attempts = 1 + (random() % 3);
    for (std::size_t attempt = 0; attempt < attempts; ++attempt) {
        const std::vector<Clip>& clips = document.find_track(first)->clips;
        const Clip& picture = clips[random() % clips.size()];
        if (picture.link.is_valid()) {
            continue;
        }
        const ClipId picture_id = picture.id;
        const Ticks start = picture.start;
        const Ticks duration = picture.duration;
        const TrackId other = document.tracks()[1 + (random() % (document.tracks().size() - 1))].id;

        const auto sound = document.add_clip(other, "linked.wav", 0, start, duration,
                                             duration + static_cast<Ticks>(random() % 200));
        if (!sound) {
            continue;  // the span is occupied on that track
        }
        EXPECT_TRUE(document.link_clips({picture_id, sound.value()}).has_value());
    }
    return document;
}

/// The tracks holding a member of `clip`'s link group.
std::vector<TrackId> member_tracks(const Document& document, ClipId clip) {
    std::vector<TrackId> tracks;
    for (const ClipId member : document.linked_clips(clip)) {
        tracks.push_back(document.track_of_clip(member)->id);
    }
    return tracks;
}

bool holds_member(const std::vector<TrackId>& tracks, TrackId id) {
    return std::find(tracks.begin(), tracks.end(), id) != tracks.end();
}

/// Everything a trim might be asked to preserve, gathered in one place.
struct Snapshot {
    Ticks sequence_end = 0;
    std::vector<Clip> clips;
};

Snapshot snapshot(const Document& document, TrackId track) {
    Snapshot state;
    state.clips = document.find_track(track)->clips;
    for (const Clip& clip : state.clips) {
        state.sequence_end = std::max(state.sequence_end, clip.start + clip.duration);
    }
    return state;
}

const Clip* find(const std::vector<Clip>& clips, ClipId id) {
    for (const Clip& clip : clips) {
        if (clip.id == id) {
            return &clip;
        }
    }
    return nullptr;
}

/// The document invariants, re-checked from outside the document. If a trim
/// could produce an illegal state, `replace_track_clips` should have refused it
/// -- this catches the case where it did not.
void check_track_is_legal(const Document& document, TrackId track) {
    const std::vector<Clip>& clips = document.find_track(track)->clips;
    for (std::size_t i = 0; i < clips.size(); ++i) {
        const Clip& clip = clips[i];
        EXPECT_GT(clip.duration, 0) << "clip " << clip.id.value() << " has no length";
        EXPECT_GE(clip.start, 0) << "clip " << clip.id.value() << " starts before zero";
        EXPECT_GE(clip.source_in, 0) << "clip " << clip.id.value() << " reads before its source";
        EXPECT_LE(clip.source_in + clip.duration, clip.source_duration)
            << "clip " << clip.id.value() << " reads past the end of its source";
        if (i > 0) {
            EXPECT_GE(clip.start, clips[i - 1].start + clips[i - 1].duration)
                << "clips " << clips[i - 1].id.value() << " and " << clip.id.value() << " overlap";
        }
    }
}

bool has_clip_after(const std::vector<Clip>& clips, const Clip& subject) {
    return std::any_of(clips.begin(), clips.end(),
                       [&subject](const Clip& other) { return other.start > subject.start; });
}

void check_document_is_legal(const Document& document) {
    for (const Track& track : document.tracks()) {
        check_track_is_legal(document, track.id);
    }
}

/// Sync lock's own property: every sync-locked track's downstream material moved
/// by exactly the amount the sequence moved, and nothing upstream of the ripple
/// point moved at all.
///
/// This is the invariant D15 was raised about. A ripple that shifted the trimmed
/// track and left another one behind satisfies every per-track check above and
/// still silently desyncs the edit.
void check_sync_locked_tracks_kept_step(const std::vector<Track>& before, const Document& after,
                                        const std::vector<TrackId>& members, ClipId id,
                                        Ticks point) {
    const Clip* now = after.find_clip(id);
    ASSERT_NE(now, nullptr);
    const Ticks shift = (now->start + now->duration) - point;

    for (const Track& was : before) {
        // A member's track was trimmed, not shifted, so it is not a bystander
        // and this check does not apply to it.
        if (holds_member(members, was.id)) {
            continue;
        }
        const Track* is_now = after.find_track(was.id);
        ASSERT_NE(is_now, nullptr);
        for (const Clip& clip : was.clips) {
            const Clip* moved = find(is_now->clips, clip.id);
            ASSERT_NE(moved, nullptr);
            const Ticks expected = clip.start + (was.sync_locked && clip.start >= point ? shift : 0);
            EXPECT_EQ(moved->start, expected)
                << "clip " << clip.id.value() << " on track " << was.id.value()
                << (was.sync_locked ? " did not keep step with the ripple"
                                    : " moved despite its sync lock being off");
            EXPECT_EQ(moved->duration, clip.duration) << "sync lock shifts, it never trims";
            EXPECT_EQ(moved->source_in, clip.source_in);
        }
    }
}

/// The invariant D16 exists for: a trim moves every member of a link by the same
/// amounts, so it never introduces drift.
///
/// Stated as deltas rather than as absolute alignment, and the difference is not
/// pedantry -- the first version of this check asserted that members share a
/// span, and the fuzz refuted it. A ripple elsewhere shifts sync-locked tracks;
/// if a link has its picture on the rippled track and its audio on a track whose
/// sync lock the user turned off, the pair legitimately comes apart. Trims must
/// not *add* to that offset, which is what this asserts. See ADR 011.
void check_link_members_moved_together(const std::vector<Clip>& before, const Document& after,
                                       ClipId clip, TrimKind kind) {
    const std::vector<ClipId> group = after.linked_clips(clip);
    const Clip* was_first = find(before, group.front());
    const Clip* now_first = after.find_clip(group.front());
    ASSERT_NE(was_first, nullptr);
    ASSERT_NE(now_first, nullptr);

    const Ticks moved = now_first->start - was_first->start;
    const Ticks resized = now_first->duration - was_first->duration;
    const Ticks slipped = now_first->source_in - was_first->source_in;

    for (const ClipId id : group) {
        const Clip* was = find(before, id);
        const Clip* now = after.find_clip(id);
        ASSERT_NE(was, nullptr);
        ASSERT_NE(now, nullptr);
        EXPECT_EQ(now->start - was->start, moved)
            << to_string(kind) << ": clip " << id.value() << " moved a different distance";
        EXPECT_EQ(now->duration - was->duration, resized)
            << to_string(kind) << ": clip " << id.value() << " was trimmed by a different amount";
        EXPECT_EQ(now->source_in - was->source_in, slipped)
            << to_string(kind) << ": clip " << id.value() << " took a different part of its source";
    }
}

/// The property that distinguishes each operation from the others.
void check_operation_preserved_what_it_should(TrimKind kind, ClipId id, const Snapshot& before,
                                              const Snapshot& after) {
    const Clip* was = find(before.clips, id);
    const Clip* now = find(after.clips, id);
    ASSERT_NE(was, nullptr);
    ASSERT_NE(now, nullptr);

    switch (kind) {
        case TrimKind::slip:
            // Invisible in the layout: only the content moves, and only for
            // this clip.
            EXPECT_EQ(now->start, was->start);
            EXPECT_EQ(now->duration, was->duration);
            EXPECT_EQ(after.sequence_end, before.sequence_end);
            for (const Clip& clip : after.clips) {
                if (clip.id != id) {
                    EXPECT_EQ(clip, *find(before.clips, clip.id)) << "slip touched another clip";
                }
            }
            break;

        case TrimKind::roll:
            // Only the shared edit point moves, so the sequence keeps its length
            // and the clip's own start does not move.
            EXPECT_EQ(now->start, was->start);
            EXPECT_EQ(after.sequence_end, before.sequence_end);
            break;

        case TrimKind::slide:
            // The clip's own content and length are untouched; it is the
            // neighbours that absorb the movement.
            EXPECT_EQ(now->source_in, was->source_in);
            EXPECT_EQ(now->duration, was->duration);
            // A slide only preserves the sequence length while something lies
            // beyond the clip to hold the end in place. Sliding the last clip on
            // a track moves the end of the sequence with it, and that is not a
            // defect -- there is nothing there to absorb it.
            if (has_clip_after(before.clips, *was)) {
                EXPECT_EQ(after.sequence_end, before.sequence_end);
            }
            break;

        case TrimKind::ripple_in:
            // Anchored start, and the out point is what moved.
            EXPECT_EQ(now->start, was->start);
            EXPECT_EQ(now->source_in - was->source_in, was->duration - now->duration);
            break;

        case TrimKind::ripple_out:
            EXPECT_EQ(now->start, was->start);
            EXPECT_EQ(now->source_in, was->source_in);
            break;
    }
}

/// Everything after the trimmed clip must move rigidly, so the gaps between
/// those clips survive a ripple unchanged. A ripple that "closed up to the next
/// clip" instead would pass every check above and silently eat a gap.
void check_ripple_moved_the_tail_rigidly(ClipId id, const Snapshot& before, const Snapshot& after) {
    const Clip* was = find(before.clips, id);
    std::vector<const Clip*> later;
    for (const Clip& clip : before.clips) {
        if (clip.start > was->start) {
            later.push_back(&clip);
        }
    }
    for (std::size_t i = 1; i < later.size(); ++i) {
        const Ticks gap_before =
            later[i]->start - (later[i - 1]->start + later[i - 1]->duration);
        const Clip* left = find(after.clips, later[i - 1]->id);
        const Clip* right = find(after.clips, later[i]->id);
        ASSERT_NE(left, nullptr);
        ASSERT_NE(right, nullptr);
        EXPECT_EQ(right->start - (left->start + left->duration), gap_before)
            << "a ripple changed the spacing downstream of the edit";
        // Moving is all a ripple may do to a downstream clip. Its content and
        // length must arrive untouched.
        EXPECT_EQ(right->source_in, later[i]->source_in);
        EXPECT_EQ(right->duration, later[i]->duration);
    }
}

TEST(TrimFuzz, RandomTrimsNeverLeaveAnIllegalDocumentAndAlwaysUndo) {
    constexpr int kDocuments = 200;
    constexpr int kTrimsPerDocument = 40;

    int applied = 0;
    int refused = 0;
    // A multi-track fuzz that never actually crossed a track boundary would look
    // identical to the single-track one it replaced, and would prove nothing
    // about ADR 010. Both of these are asserted non-zero at the end.
    int crossed_tracks = 0;
    int straddle_refusals = 0;
    int linked_trims = 0;

    for (int seed = 0; seed < kDocuments; ++seed) {
        std::mt19937_64 random(0x517cc1b727220a95ULL + static_cast<std::uint64_t>(seed));
        Document document = random_document(random);
        const std::string initial = serialise(document);

        CommandStack stack;
        for (int step = 0; step < kTrimsPerDocument; ++step) {
            const Track& track = document.tracks()[random() % document.tracks().size()];
            const TrackId track_id = track.id;
            const Clip& subject = track.clips[random() % track.clips.size()];
            const ClipId id = subject.id;
            const Ticks point = subject.start + subject.duration;
            const TrimKind kind = kKinds[random() % std::size(kKinds)];
            // Deltas well past every limit as often as small ones, so clamping
            // is exercised rather than avoided.
            const Ticks delta = static_cast<Ticks>(random() % 4000) - 2000;

            const Snapshot before = snapshot(document, track_id);
            const std::vector<Track> all_before = document.tracks();
            const std::vector<TrackId> members = member_tracks(document, id);
            const auto result = stack.execute(document, make_trim(id, kind, delta));
            if (!result) {
                ++refused;
                if (result.error().to_string().find("straddles") != std::string::npos) {
                    ++straddle_refusals;
                }
                EXPECT_EQ(document.tracks(), all_before)
                    << "a refused trim changed the document";
                continue;
            }
            ++applied;
            if (members.size() > 1) {
                ++linked_trims;
                std::vector<Clip> clips_before;
                for (const Track& was : all_before) {
                    clips_before.insert(clips_before.end(), was.clips.begin(), was.clips.end());
                }
                check_link_members_moved_together(clips_before, document, id, kind);
            }
            for (const Track& was : all_before) {
                if (!holds_member(members, was.id) && *document.find_track(was.id) != was) {
                    ++crossed_tracks;
                    break;
                }
            }

            check_document_is_legal(document);
            const Snapshot after = snapshot(document, track_id);
            check_operation_preserved_what_it_should(kind, id, before, after);
            if (kind == TrimKind::ripple_in || kind == TrimKind::ripple_out) {
                check_ripple_moved_the_tail_rigidly(id, before, after);
                check_sync_locked_tracks_kept_step(all_before, document, members, id, point);
            } else {
                // Roll, slip and slide never reach a track that holds no member.
                for (const Track& was : all_before) {
                    if (!holds_member(members, was.id)) {
                        EXPECT_EQ(*document.find_track(was.id), was)
                            << to_string(kind) << " moved a track it has no business touching";
                    }
                }
            }
        }

        while (stack.can_undo()) {
            ASSERT_TRUE(stack.undo(document).has_value());
        }
        EXPECT_EQ(serialise(document), initial) << "seed " << seed << " did not undo cleanly";
    }

    // A run that mostly refuses would exercise the limits and never the edits.
    // Deterministic seeds, so these are fixed: 4393 applied against 3607
    // refused. The refusals are real -- a roll needs a butt-joined neighbour,
    // and a clip already pushed to its media limit has nowhere further to go.
    EXPECT_GT(applied, kDocuments * kTrimsPerDocument / 2)
        << "too few trims applied for this to be a meaningful check";
    EXPECT_GT(refused, 0) << "no trim ever hit a limit; the deltas are too small";
    EXPECT_GT(crossed_tracks, 0) << "no ripple ever moved a sync-locked track; ADR 010 untested";
    EXPECT_GT(straddle_refusals, 0)
        << "no ripple ever met a clip across the ripple point; the refusal path is untested";
    EXPECT_GT(linked_trims, 0) << "no trim ever landed on a linked clip; ADR 011 untested";
    std::printf("[trim fuzz] documents=%d applied=%d refused=%d crossed=%d straddled=%d linked=%d\n",
                kDocuments, applied, refused, crossed_tracks, straddle_refusals, linked_trims);
}

}  // namespace
