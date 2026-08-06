// The four trims, checked against the definitions in
// docs/adr/009-trim-model.md rather than against the implementation.
//
// Each operation is asserted on the whole track, not just the clip named. That
// is deliberate: three of the four move a neighbour or everything downstream,
// and a test that looked only at the trimmed clip would pass while the rest of
// the timeline was silently wrong.

#include "rf/timeline/trim.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <vector>

#include "rf/timeline/command.hpp"
#include "rf/timeline/serialise.hpp"

namespace {

using rf::Errc;
using rf::media::Rational;
using rf::timeline::Clip;
using rf::timeline::ClipId;
using rf::timeline::CommandStack;
using rf::timeline::Document;
using rf::timeline::LinkId;
using rf::timeline::Ticks;
using rf::timeline::TrackId;
using rf::timeline::TrackKind;
using rf::timeline::TrimKind;
using rf::timeline::TrimRange;
using rf::timeline::make_trim;
using rf::timeline::plan_trim;
using rf::timeline::serialise;
using rf::timeline::trim_range;

/// One track with three butt-joined 100-tick clips, each cut from the middle of
/// a 300-tick source so every trim has room in both directions.
///
///   source of each: [0 .................. 300)
///   clip uses:           [100 ....... 200)
///   timeline:       A[0,100) B[100,200) C[200,300)
struct Fixture {
    Document document = Document::create(Rational{1, 90000}).value();
    TrackId track;
    ClipId a;
    ClipId b;
    ClipId c;

    Fixture() {
        track = document.add_track(TrackKind::video, "V1").value();
        a = document.add_clip(track, "a.mp4", 100, 0, 100, 300).value();
        b = document.add_clip(track, "b.mp4", 100, 100, 100, 300).value();
        c = document.add_clip(track, "c.mp4", 100, 200, 100, 300).value();
    }

    [[nodiscard]] const Clip& clip(ClipId id) const { return *document.find_clip(id); }
    [[nodiscard]] const std::vector<Clip>& clips() const {
        return document.find_track(track)->clips;
    }
};

/// Applies a trim through the command stack, failing the test if it is refused.
void must_trim(Fixture& fixture, CommandStack& stack, ClipId clip, TrimKind kind, Ticks delta) {
    const auto applied = stack.execute(fixture.document, make_trim(clip, kind, delta));
    ASSERT_TRUE(applied.has_value()) << applied.error().to_string();
}

// --- ranges ------------------------------------------------------------------

TEST(TrimRangeTest, RippleInReachesSourceZeroAndOneTickOfClip) {
    const Fixture fixture;
    // source_in 100 back to 0; forward until one tick of the clip remains.
    EXPECT_EQ(trim_range(fixture.document, fixture.b, TrimKind::ripple_in).value(),
              (TrimRange{-100, 99}));
}

TEST(TrimRangeTest, RippleOutIsBoundedByTheSourceTail) {
    const Fixture fixture;
    // 300 - 100 - 100 = 100 ticks of media past the out point.
    EXPECT_EQ(trim_range(fixture.document, fixture.b, TrimKind::ripple_out).value(),
              (TrimRange{-99, 100}));
}

TEST(TrimRangeTest, RippleOutIgnoresTheNextClipBecauseItRipples) {
    // The distinguishing property of a ripple: the clip after it is not an
    // obstacle, it is cargo. A range that stopped at the neighbour would be a
    // plain trim wearing a ripple's name.
    const Fixture fixture;
    const TrimRange range = trim_range(fixture.document, fixture.b, TrimKind::ripple_out).value();
    EXPECT_EQ(range.max_delta, 100) << "clip c starts immediately after b and must not bind";
}

TEST(TrimRangeTest, RollIsBoundedByBothSidesOfTheEditPoint) {
    const Fixture fixture;
    // Right: b has 100 ticks of tail, c has 99 ticks it can give up.
    // Left:  b can shrink by 99, c can give back 100 ticks of head.
    EXPECT_EQ(trim_range(fixture.document, fixture.b, TrimKind::roll).value(),
              (TrimRange{-99, 99}));
}

TEST(TrimRangeTest, RollNeedsAButtJoinedClipAfterIt) {
    Fixture fixture;
    ASSERT_TRUE(fixture.document.move_clip(fixture.c, fixture.track, 500).has_value());

    const auto range = trim_range(fixture.document, fixture.b, TrimKind::roll);
    ASSERT_TRUE(range.has_error()) << "a roll across a gap has no shared edit point";
    EXPECT_EQ(range.error().code(), Errc::invalid_argument);

    EXPECT_TRUE(trim_range(fixture.document, fixture.c, TrimKind::roll).has_error())
        << "the last clip on a track has nothing to roll against";
}

TEST(TrimRangeTest, SlipIsBoundedOnlyByTheSource) {
    const Fixture fixture;
    EXPECT_EQ(trim_range(fixture.document, fixture.b, TrimKind::slip).value(),
              (TrimRange{-100, 100}));
}

TEST(TrimRangeTest, SlideIsBoundedByBothNeighbours) {
    const Fixture fixture;
    // Left: a can shrink by 99; right: c can give up 99.
    // Right also: a has 100 ticks of tail to extend into.
    EXPECT_EQ(trim_range(fixture.document, fixture.b, TrimKind::slide).value(),
              (TrimRange{-99, 99}));
}

TEST(TrimRangeTest, SlideIntoAGapIsBoundedByTheGap) {
    Fixture fixture;
    // Pull c away so b has 50 ticks of empty space after it, and remove a so
    // b has its start to run back to.
    ASSERT_TRUE(fixture.document.move_clip(fixture.c, fixture.track, 250).has_value());
    ASSERT_TRUE(fixture.document.remove_clip(fixture.a).has_value());

    EXPECT_EQ(trim_range(fixture.document, fixture.b, TrimKind::slide).value(),
              (TrimRange{-100, 50}));
}

TEST(TrimRangeTest, SlideIsStillBoundedByTheSourceOfAButtJoinedNeighbour) {
    // Nothing follows b, but a is butt-joined before it, so sliding right has to
    // extend a's out point -- and a only has 100 ticks of tail.
    Fixture fixture;
    ASSERT_TRUE(fixture.document.remove_clip(fixture.c).has_value());

    EXPECT_EQ(trim_range(fixture.document, fixture.b, TrimKind::slide).value(),
              (TrimRange{-99, 100}));
}

TEST(TrimRangeTest, SlideWithNoNeighbourAtAllStopsWhereTicksRunOut) {
    // Nothing on the timeline limits this slide, so the only bound left is what
    // a tick can represent. Reporting it as a number rather than as "unbounded"
    // is what stops a caller adding a delta that overflows the clip's end.
    Fixture fixture;
    ASSERT_TRUE(fixture.document.remove_clip(fixture.a).has_value());
    ASSERT_TRUE(fixture.document.remove_clip(fixture.c).has_value());

    const TrimRange range = trim_range(fixture.document, fixture.b, TrimKind::slide).value();
    EXPECT_EQ(range.min_delta, -100) << "bounded only by the start of the timeline";
    EXPECT_EQ(range.max_delta, std::numeric_limits<Ticks>::max() - 200)
        << "the clip ends at tick 200, so that is how much headroom is left";
}

TEST(Trim, ASlideToTheRepresentableLimitDoesNotOverflow) {
    // The case the optional bound used to allow through: a delta far past any
    // real limit, on a clip with nothing to stop it.
    Fixture fixture;
    ASSERT_TRUE(fixture.document.remove_clip(fixture.a).has_value());
    ASSERT_TRUE(fixture.document.remove_clip(fixture.c).has_value());

    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::slide, std::numeric_limits<Ticks>::max());
    const Clip& b = fixture.clip(fixture.b);
    EXPECT_EQ(b.start, std::numeric_limits<Ticks>::max() - 100);
    EXPECT_EQ(b.duration, 100);
    EXPECT_GT(b.start, 0) << "the start must not have wrapped";
}

TEST(TrimRangeTest, ARangeWithNoRoomIsEmptyRatherThanAnError) {
    Document document = Document::create(Rational{1, 90000}).value();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    // Consumes its source exactly: nothing to slip into, either way.
    const ClipId clip = document.add_clip(track, "a.mp4", 0, 0, 100, 100).value();

    const TrimRange range = trim_range(document, clip, TrimKind::slip).value();
    EXPECT_TRUE(range.is_empty());
    EXPECT_EQ(range, (TrimRange{0, 0}));
}

TEST(TrimRangeTest, EveryRangeContainsZero) {
    // Doing nothing is always reachable. This is what lets clamp_delta be total.
    const Fixture fixture;
    for (const TrimKind kind : {TrimKind::ripple_in, TrimKind::ripple_out, TrimKind::roll,
                                TrimKind::slip, TrimKind::slide}) {
        const auto range = trim_range(fixture.document, fixture.b, kind);
        ASSERT_TRUE(range.has_value()) << to_string(kind);
        EXPECT_TRUE(range.value().allows(0)) << to_string(kind);
    }
}

TEST(TrimRangeTest, AnUnknownClipIsNotFound) {
    const Fixture fixture;
    const auto range = trim_range(fixture.document, ClipId{9999}, TrimKind::slip);
    ASSERT_TRUE(range.has_error());
    EXPECT_EQ(range.error().code(), Errc::not_found);
}

TEST(ClampDelta, StopsAtTheNearestBoundAndPassesWhatFits) {
    const TrimRange range{-10, 20};
    EXPECT_EQ(rf::timeline::clamp_delta(range, 5), 5);
    EXPECT_EQ(rf::timeline::clamp_delta(range, 500), 20);
    EXPECT_EQ(rf::timeline::clamp_delta(range, -500), -10);
    EXPECT_EQ(rf::timeline::clamp_delta(TrimRange{0, 0}, 7), 0) << "no room means no movement";
}

// --- what each operation actually does ---------------------------------------

TEST(Trim, RippleOutMovesEverythingAfterIt) {
    Fixture fixture;
    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::ripple_out, 20);

    EXPECT_EQ(fixture.clip(fixture.a).start, 0);
    EXPECT_EQ(fixture.clip(fixture.a).duration, 100) << "a is before the edit and must not move";
    EXPECT_EQ(fixture.clip(fixture.b).start, 100);
    EXPECT_EQ(fixture.clip(fixture.b).duration, 120);
    EXPECT_EQ(fixture.clip(fixture.b).source_in, 100) << "the in point is not what was trimmed";
    EXPECT_EQ(fixture.clip(fixture.c).start, 220) << "c must ripple, not be overwritten";
    EXPECT_EQ(fixture.clip(fixture.c).duration, 100);
    EXPECT_EQ(fixture.clip(fixture.c).source_in, 100) << "a rippled clip keeps its own content";
}

TEST(Trim, RippleInKeepsTheStartAndPullsTheRestBack) {
    // The reading settled in ADR 009: material comes off the head, the clip
    // keeps its timeline start, and the sequence gets shorter by the delta.
    Fixture fixture;
    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::ripple_in, 20);

    EXPECT_EQ(fixture.clip(fixture.b).start, 100) << "the start is anchored";
    EXPECT_EQ(fixture.clip(fixture.b).source_in, 120) << "20 ticks came off the head";
    EXPECT_EQ(fixture.clip(fixture.b).duration, 80);
    EXPECT_EQ(fixture.clip(fixture.c).start, 180) << "the sequence shortened by 20";
    EXPECT_EQ(fixture.clip(fixture.a).duration, 100);
}

TEST(Trim, RippleLeavesGapsDownstreamIntact) {
    // Everything after the edit moves rigidly. A ripple implemented as "close up
    // to the next clip" would silently swallow the gap.
    Fixture fixture;
    ASSERT_TRUE(fixture.document.move_clip(fixture.c, fixture.track, 250).has_value());
    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::ripple_out, 10);

    EXPECT_EQ(fixture.clip(fixture.b).start + fixture.clip(fixture.b).duration, 210);
    EXPECT_EQ(fixture.clip(fixture.c).start, 260) << "the 50-tick gap must survive the ripple";
}

TEST(Trim, RollMovesOnlyTheSharedEditPoint) {
    Fixture fixture;
    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::roll, 30);

    EXPECT_EQ(fixture.clip(fixture.b).start, 100);
    EXPECT_EQ(fixture.clip(fixture.b).duration, 130);
    EXPECT_EQ(fixture.clip(fixture.c).start, 230);
    EXPECT_EQ(fixture.clip(fixture.c).source_in, 130) << "the incoming clip gives up its head";
    EXPECT_EQ(fixture.clip(fixture.c).duration, 70);
    EXPECT_EQ(fixture.clip(fixture.c).start + fixture.clip(fixture.c).duration, 300)
        << "a roll must not change the sequence length";
    EXPECT_EQ(fixture.clip(fixture.a).duration, 100);
}

TEST(Trim, SlipChangesTheContentAndNothingElse) {
    Fixture fixture;
    const std::vector<Clip> before = fixture.clips();
    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::slip, -40);

    EXPECT_EQ(fixture.clip(fixture.b).source_in, 60);
    EXPECT_EQ(fixture.clip(fixture.b).start, 100) << "slip is invisible in the layout";
    EXPECT_EQ(fixture.clip(fixture.b).duration, 100);
    EXPECT_EQ(fixture.clip(fixture.a), before[0]);
    EXPECT_EQ(fixture.clip(fixture.c), before[2]);
}

TEST(Trim, SlideMovesTheClipAndLetsNeighboursAbsorbIt) {
    Fixture fixture;
    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::slide, 25);

    EXPECT_EQ(fixture.clip(fixture.b).start, 125);
    EXPECT_EQ(fixture.clip(fixture.b).source_in, 100) << "a slide never changes its own content";
    EXPECT_EQ(fixture.clip(fixture.b).duration, 100);

    EXPECT_EQ(fixture.clip(fixture.a).start, 0);
    EXPECT_EQ(fixture.clip(fixture.a).duration, 125) << "the outgoing clip extends";
    EXPECT_EQ(fixture.clip(fixture.c).start, 225);
    EXPECT_EQ(fixture.clip(fixture.c).source_in, 125) << "the incoming clip gives up its head";
    EXPECT_EQ(fixture.clip(fixture.c).duration, 75);
    EXPECT_EQ(fixture.clip(fixture.c).start + fixture.clip(fixture.c).duration, 300)
        << "a slide must not change the sequence length";
}

TEST(Trim, SlideIntoAGapLeavesTheNeighbourAlone) {
    Fixture fixture;
    ASSERT_TRUE(fixture.document.move_clip(fixture.c, fixture.track, 250).has_value());
    const Clip c_before = fixture.clip(fixture.c);
    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::slide, 30);

    EXPECT_EQ(fixture.clip(fixture.b).start, 130);
    EXPECT_EQ(fixture.clip(fixture.a).duration, 130) << "the butt-joined side still absorbs";
    EXPECT_EQ(fixture.clip(fixture.c), c_before) << "a clip across a gap is not a neighbour";
}

// --- limits ------------------------------------------------------------------

TEST(Trim, ClampsToTheLimitRatherThanRefusingWholesale) {
    // Premiere's behaviour, and the reason clamping is in the command rather
    // than left to callers: a keyboard trim of "10 seconds" against 2 seconds of
    // available media should give you the 2 seconds.
    Fixture fixture;
    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::slip, 10'000);

    EXPECT_EQ(fixture.clip(fixture.b).source_in, 200) << "clamped to the end of the source";
    EXPECT_EQ(fixture.clip(fixture.b).source_in + fixture.clip(fixture.b).duration, 300);
}

TEST(Trim, RefusesATrimWithNoRoomAndLeavesNoUndoEntry) {
    Document document = Document::create(Rational{1, 90000}).value();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    const ClipId clip = document.add_clip(track, "a.mp4", 0, 0, 100, 100).value();
    const std::string before = serialise(document);

    CommandStack stack;
    const auto refused = stack.execute(document, make_trim(clip, TrimKind::slip, 50));
    ASSERT_TRUE(refused.has_error());
    EXPECT_EQ(refused.error().code(), Errc::invalid_argument);
    EXPECT_FALSE(stack.can_undo()) << "a refused edit must not become an undo entry";
    EXPECT_EQ(serialise(document), before);
}

TEST(Trim, RefusesToTrimAnUnknownClip) {
    Fixture fixture;
    CommandStack stack;
    const auto refused =
        stack.execute(fixture.document, make_trim(ClipId{9999}, TrimKind::slip, 10));
    ASSERT_TRUE(refused.has_error());
    EXPECT_EQ(refused.error().code(), Errc::not_found);
    EXPECT_FALSE(stack.can_undo());
}

TEST(Trim, ARippleNeverReachesPastTheSource) {
    Fixture fixture;
    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::ripple_out, 10'000);

    const Clip& b = fixture.clip(fixture.b);
    EXPECT_EQ(b.duration, 200) << "clamped to the 100 ticks of tail";
    EXPECT_EQ(b.source_in + b.duration, b.source_duration);
    EXPECT_EQ(fixture.clip(fixture.c).start, 300) << "and c rippled the full clamped amount";
}

// --- undo --------------------------------------------------------------------

TEST(Trim, UndoRestoresTheWholeTrackByteForByte) {
    Fixture fixture;
    const std::string before = serialise(fixture.document);
    CommandStack stack;

    for (const TrimKind kind : {TrimKind::ripple_in, TrimKind::ripple_out, TrimKind::roll,
                                TrimKind::slip, TrimKind::slide}) {
        must_trim(fixture, stack, fixture.b, kind, 17);
        EXPECT_NE(serialise(fixture.document), before) << to_string(kind) << " changed nothing";
        ASSERT_TRUE(stack.undo(fixture.document).has_value());
        EXPECT_EQ(serialise(fixture.document), before) << to_string(kind) << " did not undo";
    }
}

TEST(Trim, RedoReproducesTheSameDocument) {
    Fixture fixture;
    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::slide, 40);
    const std::string after = serialise(fixture.document);

    ASSERT_TRUE(stack.undo(fixture.document).has_value());
    ASSERT_TRUE(stack.redo(fixture.document).has_value());
    EXPECT_EQ(serialise(fixture.document), after);
}

TEST(Trim, UndoNamesTheOperationForTheMenu) {
    Fixture fixture;
    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::ripple_out, 10);
    EXPECT_EQ(stack.undo_name(), "Ripple Trim Out");
}

// --- sync lock (ADR 010) -----------------------------------------------------
//
// Sync lock moves *downstream material on other tracks* so it stays where the
// user put it relative to the picture. It never trims anything, so it is not
// what keeps a paired A/V clip together -- that is clip linking, which does not
// exist yet (D16).

/// The three-clip video track of `Fixture`, plus an audio track carrying a
/// single clip whose position relative to the picture is what sync lock is for.
struct SyncFixture : Fixture {
    TrackId audio;
    ClipId music;

    explicit SyncFixture(Ticks music_start) {
        audio = document.add_track(TrackKind::audio, "A1").value();
        music = document.add_clip(audio, "music.wav", 0, music_start, 100, 1000).value();
    }

    [[nodiscard]] const std::vector<Clip>& audio_clips() const {
        return document.find_track(audio)->clips;
    }
};

TEST(SyncLock, IsOnByDefaultAsInPremiere) {
    const Fixture fixture;
    EXPECT_TRUE(fixture.document.find_track(fixture.track)->sync_locked);
}

TEST(SyncLock, ARippleShiftsDownstreamMaterialOnOtherTracks) {
    SyncFixture fixture(300);  // music sits just past the end of the video track
    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::ripple_out, 20);

    EXPECT_EQ(fixture.clip(fixture.c).start, 220);
    EXPECT_EQ(fixture.clip(fixture.music).start, 320)
        << "the music must keep its position relative to the picture";
    EXPECT_EQ(fixture.clip(fixture.music).duration, 100) << "sync lock shifts, it never trims";
    EXPECT_EQ(fixture.clip(fixture.music).source_in, 0);
}

TEST(SyncLock, ARippleOfTheInEdgePullsOtherTracksBack) {
    // The ripple point is the clip's out point for both edges, so material after
    // the *end* of b closes up -- even though the frames came off b's head.
    SyncFixture fixture(300);
    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::ripple_in, 20);

    EXPECT_EQ(fixture.clip(fixture.c).start, 180);
    EXPECT_EQ(fixture.clip(fixture.music).start, 280);
}

TEST(SyncLock, MaterialBeforeTheRipplePointDoesNotMove) {
    SyncFixture fixture(0);  // music sits under clip a, before the edit
    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::ripple_out, 20);

    EXPECT_EQ(fixture.clip(fixture.music).start, 0)
        << "a ripple only moves what comes after the edit point";
}

TEST(SyncLock, ClearingItLeavesTheTrackWhereItIs) {
    SyncFixture fixture(300);
    ASSERT_TRUE(fixture.document.set_track_sync_locked(fixture.audio, false).has_value());

    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::ripple_out, 20);

    EXPECT_EQ(fixture.clip(fixture.c).start, 220) << "the trimmed track always ripples";
    EXPECT_EQ(fixture.clip(fixture.music).start, 300) << "an unlocked track is left alone";
}

TEST(SyncLock, RollSlipAndSlideNeverReachAnotherTrack) {
    // None of them changes the length of the sequence, so there is no downstream
    // material to move. A sync lock that fired on these would shift clips for no
    // reason at all.
    for (const TrimKind kind : {TrimKind::roll, TrimKind::slip, TrimKind::slide}) {
        SyncFixture fixture(300);
        const std::vector<Clip> before = fixture.audio_clips();
        CommandStack stack;
        must_trim(fixture, stack, fixture.b, kind, 25);
        EXPECT_EQ(fixture.audio_clips(), before) << to_string(kind) << " moved another track";
    }
}

TEST(SyncLock, ARippleIsRefusedWhenAClipStraddlesTheRipplePoint) {
    // No shift of the audio track keeps it legal: the clip's head is pinned by
    // the material before the point and its tail is in the region that moves.
    // Shifting round it would desync everything after it, silently.
    SyncFixture fixture(150);  // spans 150..250, and the ripple point is at 200
    CommandStack stack;
    const std::vector<Clip> before = fixture.audio_clips();

    const auto refused =
        stack.execute(fixture.document, make_trim(fixture.b, TrimKind::ripple_out, 20));
    ASSERT_TRUE(refused.has_error());
    EXPECT_EQ(refused.error().code(), Errc::invalid_argument);
    EXPECT_NE(refused.error().to_string().find("straddles"), std::string::npos)
        << refused.error().to_string();

    EXPECT_EQ(fixture.audio_clips(), before);
    EXPECT_EQ(fixture.clip(fixture.c).start, 200) << "the trimmed track must not have moved either";
    EXPECT_FALSE(stack.can_undo());
}

TEST(SyncLock, DroppingTheSyncLockLetsTheRipplePastAStraddlingClip) {
    // The error tells the user this is the way out, so it had better work.
    SyncFixture fixture(150);
    ASSERT_TRUE(fixture.document.set_track_sync_locked(fixture.audio, false).has_value());

    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::ripple_out, 20);
    EXPECT_EQ(fixture.clip(fixture.c).start, 220);
}

/// The audio track with a voice-over ending at 160 and the music starting at the
/// ripple point, so the music has only 40 ticks of room to move left.
SyncFixture crowded_audio() {
    SyncFixture fixture(200);
    EXPECT_TRUE(fixture.document.add_clip(fixture.audio, "vo.wav", 0, 60, 100, 1000).has_value());
    return fixture;
}

TEST(SyncLock, ANeighbouringTrackNarrowsTheReachableRange) {
    // The video track alone would allow the out point back by 99. The music
    // cannot follow more than 40 without hitting the voice-over, and a ripple
    // that moved one and not the other is exactly the desync being prevented.
    const SyncFixture fixture = crowded_audio();

    const TrimRange range = trim_range(fixture.document, fixture.b, TrimKind::ripple_out).value();
    EXPECT_EQ(range.min_delta, -40) << "the music cannot pass the voice-over ending at 160";
    EXPECT_EQ(range.max_delta, 100) << "unchanged: nothing bounds it on the right";
}

TEST(SyncLock, ARippleClampedByAnotherTrackStillMovesBothTogether) {
    SyncFixture fixture = crowded_audio();
    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::ripple_out, -500);

    EXPECT_EQ(fixture.clip(fixture.b).duration, 60) << "clamped to -40";
    EXPECT_EQ(fixture.clip(fixture.c).start, 160);
    EXPECT_EQ(fixture.clip(fixture.music).start, 160)
        << "the music moved by the same 40, butting against the voice-over";
}

TEST(SyncLock, UndoRestoresEveryTrackTheRippleTouched) {
    SyncFixture fixture(300);
    const std::string before = serialise(fixture.document);

    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::ripple_out, 20);
    EXPECT_NE(serialise(fixture.document), before);

    ASSERT_TRUE(stack.undo(fixture.document).has_value());
    EXPECT_EQ(serialise(fixture.document), before)
        << "undoing a ripple has to put back every track it moved, not just one";
}

TEST(SyncLock, ATrackWithNothingAfterThePointIsNotPartOfTheEdit) {
    SyncFixture fixture(0);
    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::ripple_out, 20);

    const auto planned = plan_trim(fixture.document, fixture.b, TrimKind::ripple_out, 10);
    ASSERT_TRUE(planned.has_value());
    EXPECT_EQ(planned.value().size(), 1u)
        << "a track with no downstream material must not enter the undo record";
}

// --- linked clips (ADR 011) --------------------------------------------------
//
// The defect D16 named: a ripple on a picture clip that leaves its audio at the
// old length. Sync lock cannot fix it, because sync lock shifts and never trims.

/// `Fixture`'s video track, plus an audio track carrying a clip aligned with `b`
/// and linked to it -- the picture and its sound.
struct LinkFixture : Fixture {
    TrackId audio;
    ClipId sound;
    ClipId after;

    explicit LinkFixture(Ticks sound_source_duration = 300) {
        audio = document.add_track(TrackKind::audio, "A1").value();
        sound = document.add_clip(audio, "b.wav", 100, 100, 100, sound_source_duration).value();
        after = document.add_clip(audio, "next.wav", 0, 200, 100, 1000).value();
        EXPECT_TRUE(document.link_clips({b, sound}).has_value());
    }

    [[nodiscard]] const std::vector<Clip>& audio_clips() const {
        return document.find_track(audio)->clips;
    }
};

TEST(LinkedClips, ARippleTrimsEveryMemberByTheSameAmount) {
    LinkFixture fixture;
    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::ripple_out, 20);

    EXPECT_EQ(fixture.clip(fixture.b).duration, 120);
    EXPECT_EQ(fixture.clip(fixture.sound).duration, 120)
        << "the audio must be trimmed, not merely shifted";
    EXPECT_EQ(fixture.clip(fixture.sound).start, 100);
    EXPECT_EQ(fixture.clip(fixture.b).start, fixture.clip(fixture.sound).start);
    EXPECT_EQ(fixture.clip(fixture.b).duration, fixture.clip(fixture.sound).duration)
        << "the pair must still be aligned after the trim";
}

TEST(LinkedClips, TrimmingEitherMemberTrimsBoth) {
    // The user clicks the audio as readily as the picture.
    LinkFixture fixture;
    CommandStack stack;
    must_trim(fixture, stack, fixture.sound, TrimKind::ripple_out, 20);

    EXPECT_EQ(fixture.clip(fixture.b).duration, 120);
    EXPECT_EQ(fixture.clip(fixture.sound).duration, 120);
}

TEST(LinkedClips, EachMemberRipplesItsOwnDownstreamMaterial) {
    LinkFixture fixture;
    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::ripple_out, 20);

    EXPECT_EQ(fixture.clip(fixture.c).start, 220) << "video downstream";
    EXPECT_EQ(fixture.clip(fixture.after).start, 220) << "audio downstream";
}

TEST(LinkedClips, AMemberTrackIsNotShiftedTwice) {
    // The audio track is moved by the trim, so the sync-lock sweep must skip it.
    // Shifting it again would move the downstream audio by 40 where the video
    // moved by 20 -- a desync produced by the very feature meant to prevent one.
    LinkFixture fixture;
    ASSERT_TRUE(fixture.document.find_track(fixture.audio)->sync_locked)
        << "sanity: the track is sync-locked, so the sweep would reach it";

    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::ripple_out, 20);
    EXPECT_EQ(fixture.clip(fixture.after).start, 220) << "moved once, by 20, not twice";
}

TEST(LinkedClips, TheRangeIsTheIntersectionOfEveryMember) {
    // The picture has 100 ticks of tail; the sound has 20. A ripple that used
    // the picture's limit would run the audio past the end of its media.
    LinkFixture fixture(220);  // sound: source_in 100 + duration 100 + 20 tail

    const TrimRange range = trim_range(fixture.document, fixture.b, TrimKind::ripple_out).value();
    EXPECT_EQ(range.max_delta, 20) << "the shorter source binds the pair";
}

TEST(LinkedClips, ATrimClampsToTheShorterMemberAndStaysAligned) {
    LinkFixture fixture(220);
    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::ripple_out, 10'000);

    const Clip& picture = fixture.clip(fixture.b);
    const Clip& sound = fixture.clip(fixture.sound);
    EXPECT_EQ(picture.duration, 120) << "clamped to the sound's 20 ticks of tail";
    EXPECT_EQ(sound.duration, 120);
    EXPECT_EQ(sound.source_in + sound.duration, sound.source_duration) << "sound is at its limit";
    EXPECT_LT(picture.source_in + picture.duration, picture.source_duration)
        << "the picture still has media left, and stopped anyway";
}

TEST(LinkedClips, SlipAppliesToBothMembers) {
    LinkFixture fixture;
    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::slip, -40);

    EXPECT_EQ(fixture.clip(fixture.b).source_in, 60);
    EXPECT_EQ(fixture.clip(fixture.sound).source_in, 60)
        << "slipping the picture and not the sound is a lip-sync error";
    EXPECT_EQ(fixture.clip(fixture.sound).start, 100);
}

TEST(LinkedClips, ARollNeedsANeighbourAfterEveryMember) {
    LinkFixture fixture;
    // Pull the audio's next clip away, leaving a gap after the sound only.
    ASSERT_TRUE(fixture.document.move_clip(fixture.after, fixture.audio, 500).has_value());

    const auto range = trim_range(fixture.document, fixture.b, TrimKind::roll);
    ASSERT_TRUE(range.has_error()) << "a roll on one side only would desync the pair";
    EXPECT_NE(range.error().to_string().find(to_string(fixture.sound)), std::string::npos)
        << "the error must name the member with no neighbour: " << range.error().to_string();
}

TEST(LinkedClips, UndoRestoresEveryMemberAndEveryTrack) {
    LinkFixture fixture;
    const std::string before = serialise(fixture.document);
    CommandStack stack;

    for (const TrimKind kind : {TrimKind::ripple_in, TrimKind::ripple_out, TrimKind::roll,
                                TrimKind::slip}) {
        must_trim(fixture, stack, fixture.b, kind, 17);
        EXPECT_NE(serialise(fixture.document), before) << to_string(kind);
        ASSERT_TRUE(stack.undo(fixture.document).has_value());
        EXPECT_EQ(serialise(fixture.document), before) << to_string(kind) << " did not undo";
    }
}

TEST(LinkedClips, UnlinkingLetsTheMembersTrimSeparately) {
    LinkFixture fixture;
    const LinkId link = fixture.clip(fixture.b).link;
    ASSERT_TRUE(link.is_valid());
    ASSERT_TRUE(fixture.document.unlink(link).has_value());

    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::ripple_out, 20);
    EXPECT_EQ(fixture.clip(fixture.b).duration, 120);
    EXPECT_EQ(fixture.clip(fixture.sound).duration, 100) << "no longer linked";
}

TEST(LinkedClips, AnUnlockedTrackCanDesyncALinkAndThatIsTheUsersChoice) {
    // Found by the fuzz, after a first draft of ADR 011 claimed drift was
    // impossible by construction. It is not: a ripple upstream shifts sync-locked
    // tracks, and a track the user unlocked deliberately stays put. Premiere
    // behaves the same way, which is why it has an out-of-sync indicator --
    // ReelForge has none yet (D18), so this is stated here rather than implied.
    LinkFixture fixture;
    ASSERT_TRUE(fixture.document.set_track_sync_locked(fixture.audio, false).has_value());

    CommandStack stack;
    must_trim(fixture, stack, fixture.a, TrimKind::ripple_out, 20);

    EXPECT_EQ(fixture.clip(fixture.b).start, 120) << "the picture rippled with its own track";
    EXPECT_EQ(fixture.clip(fixture.sound).start, 100) << "the audio stayed, as asked";
}

TEST(LinkedClips, ALaterTrimKeepsADesyncedPairsOffsetRatherThanAddingToIt) {
    LinkFixture fixture;
    ASSERT_TRUE(fixture.document.set_track_sync_locked(fixture.audio, false).has_value());
    CommandStack stack;
    must_trim(fixture, stack, fixture.a, TrimKind::ripple_out, 20);

    const Ticks offset = fixture.clip(fixture.b).start - fixture.clip(fixture.sound).start;
    ASSERT_EQ(offset, 20) << "sanity: the pair is now 20 ticks apart";

    must_trim(fixture, stack, fixture.b, TrimKind::ripple_out, 10);
    EXPECT_EQ(fixture.clip(fixture.b).duration, fixture.clip(fixture.sound).duration)
        << "both members trimmed by the same 10";
    EXPECT_EQ(fixture.clip(fixture.b).start - fixture.clip(fixture.sound).start, offset)
        << "a trim must not add to an offset it did not create";
}

TEST(LinkedClips, AnUnlinkedClipIsAGroupOfOne) {
    const Fixture fixture;
    EXPECT_EQ(fixture.document.linked_clips(fixture.b), std::vector<ClipId>{fixture.b});
}

// --- planning ----------------------------------------------------------------

TEST(PlanTrim, AgreesWithWhatTheCommandDoes) {
    // The UI draws the plan and the command commits it. If the two could
    // disagree, a drag would preview one edit and perform another.
    Fixture fixture;
    const auto planned = plan_trim(fixture.document, fixture.b, TrimKind::roll, 30);
    ASSERT_TRUE(planned.has_value()) << planned.error().to_string();

    ASSERT_EQ(planned.value().size(), 1u) << "a roll never reaches beyond its own track";
    EXPECT_EQ(planned.value()[0].track, fixture.track);

    CommandStack stack;
    must_trim(fixture, stack, fixture.b, TrimKind::roll, 30);
    EXPECT_EQ(fixture.clips(), planned.value()[0].clips);
}

TEST(PlanTrim, ClampsTheSameWayTheCommandDoes) {
    const Fixture fixture;
    const auto planned = plan_trim(fixture.document, fixture.b, TrimKind::slip, 10'000);
    ASSERT_TRUE(planned.has_value());
    EXPECT_EQ(planned.value()[0].clips[1].source_in, 200);
}

TEST(PlanTrim, DoesNotTouchTheDocument) {
    Fixture fixture;
    const std::string before = serialise(fixture.document);
    ASSERT_TRUE(plan_trim(fixture.document, fixture.b, TrimKind::ripple_out, 50).has_value());
    EXPECT_EQ(serialise(fixture.document), before);
}

}  // namespace
