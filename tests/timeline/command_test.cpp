#include "rf/timeline/command.hpp"

#include <gtest/gtest.h>

#include <string>

#include "rf/timeline/serialise.hpp"

namespace {

using rf::Errc;
using rf::media::Rational;
using rf::timeline::ClipId;
using rf::timeline::CommandStack;
using rf::timeline::Document;
using rf::timeline::TrackId;
using rf::timeline::TrackKind;
using rf::timeline::serialise;

/// Long enough that the source is never the binding constraint in these tests.
constexpr rf::timeline::Ticks kSourceTicks = 1'000'000;

Document make_document() {
    auto document = Document::create(Rational{1, 90000}, Rational{30, 1});
    EXPECT_TRUE(document.has_value());
    return std::move(document).value();
}

/// Applies a command and asserts it succeeded.
void must_execute(CommandStack& stack, Document& document,
                  std::unique_ptr<rf::timeline::Command> command) {
    const auto result = stack.execute(document, std::move(command));
    ASSERT_TRUE(result.has_value()) << result.error().to_string();
}

// --- stack behaviour ---------------------------------------------------------

TEST(CommandStack, StartsEmpty) {
    const CommandStack stack;
    EXPECT_FALSE(stack.can_undo());
    EXPECT_FALSE(stack.can_redo());
    EXPECT_EQ(stack.undo_depth(), 0u);
}

TEST(CommandStack, UndoOnAnEmptyStackIsAnError) {
    Document document = make_document();
    CommandStack stack;
    EXPECT_EQ(stack.undo(document).error().code(), Errc::not_found);
    EXPECT_EQ(stack.redo(document).error().code(), Errc::not_found);
}

TEST(CommandStack, RejectsANullCommand) {
    Document document = make_document();
    CommandStack stack;
    EXPECT_TRUE(stack.execute(document, nullptr).has_error());
}

TEST(CommandStack, AFailedCommandDoesNotEnterTheHistory) {
    // Otherwise the next undo would "reverse" an edit that never happened, and
    // the document would drift away from what the user sees.
    Document document = make_document();
    CommandStack stack;

    const auto result = stack.execute(document, rf::timeline::make_remove_clip(ClipId{404}));
    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(stack.undo_depth(), 0u);
    EXPECT_FALSE(stack.can_undo());
}

TEST(CommandStack, ExecutingDiscardsTheRedoBranch) {
    Document document = make_document();
    CommandStack stack;
    must_execute(stack, document, rf::timeline::make_add_track(TrackKind::video, "V1"));
    must_execute(stack, document, rf::timeline::make_add_track(TrackKind::video, "V2"));

    ASSERT_TRUE(stack.undo(document).has_value());
    EXPECT_EQ(stack.redo_depth(), 1u);

    must_execute(stack, document, rf::timeline::make_add_track(TrackKind::audio, "A1"));
    EXPECT_EQ(stack.redo_depth(), 0u) << "editing after undo must abandon the redo branch";
}

TEST(CommandStack, ReportsNamesForTheMenu) {
    Document document = make_document();
    CommandStack stack;
    EXPECT_TRUE(stack.undo_name().empty());

    must_execute(stack, document, rf::timeline::make_add_track(TrackKind::video, "V1"));
    EXPECT_EQ(stack.undo_name(), "Add Track");

    ASSERT_TRUE(stack.undo(document).has_value());
    EXPECT_EQ(stack.redo_name(), "Add Track");
    EXPECT_TRUE(stack.undo_name().empty());
}

// --- individual inverses -----------------------------------------------------
// Each of these checks the property the fuzz checks in bulk, but named, so a
// failure says which inverse is wrong instead of only that one is.

TEST(Command, AddTrackUndoesToTheOriginalBytes) {
    Document document = make_document();
    const std::string before = serialise(document);

    CommandStack stack;
    must_execute(stack, document, rf::timeline::make_add_track(TrackKind::video, "V1"));
    EXPECT_NE(serialise(document), before);

    ASSERT_TRUE(stack.undo(document).has_value());
    EXPECT_EQ(serialise(document), before) << "add-track's inverse did not return the id counter";
}

TEST(Command, AddClipUndoesToTheOriginalBytes) {
    Document document = make_document();
    CommandStack stack;
    must_execute(stack, document, rf::timeline::make_add_track(TrackKind::video, "V1"));
    const TrackId track = document.tracks().front().id;
    const std::string before = serialise(document);

    must_execute(stack, document, rf::timeline::make_add_clip(track, "a.mp4", 10, 0, 100, kSourceTicks));
    ASSERT_TRUE(stack.undo(document).has_value());
    EXPECT_EQ(serialise(document), before);
}

TEST(Command, RemoveClipRestoresEveryField) {
    Document document = make_document();
    CommandStack stack;
    must_execute(stack, document, rf::timeline::make_add_track(TrackKind::video, "V1"));
    const TrackId track = document.tracks().front().id;
    must_execute(stack, document, rf::timeline::make_add_clip(track, "a.mp4", 77, 500, 250, kSourceTicks));
    const ClipId clip = document.tracks().front().clips.front().id;
    must_execute(stack, document, rf::timeline::make_set_clip_enabled(clip, false));

    const std::string before = serialise(document);
    must_execute(stack, document, rf::timeline::make_remove_clip(clip));
    ASSERT_TRUE(stack.undo(document).has_value());

    EXPECT_EQ(serialise(document), before)
        << "restoring a removed clip lost a field -- source_in, flags or position";
}

TEST(Command, RemoveTrackRestoresItsClipsAndItsPosition) {
    Document document = make_document();
    CommandStack stack;
    must_execute(stack, document, rf::timeline::make_add_track(TrackKind::video, "V1"));
    must_execute(stack, document, rf::timeline::make_add_track(TrackKind::video, "V2"));
    must_execute(stack, document, rf::timeline::make_add_track(TrackKind::video, "V3"));
    const TrackId middle = document.tracks()[1].id;
    must_execute(stack, document, rf::timeline::make_add_clip(middle, "a.mp4", 0, 0, 100, kSourceTicks));
    must_execute(stack, document, rf::timeline::make_add_clip(middle, "b.mp4", 0, 200, 100, kSourceTicks));

    const std::string before = serialise(document);
    must_execute(stack, document, rf::timeline::make_remove_track(middle));
    ASSERT_TRUE(stack.undo(document).has_value());

    EXPECT_EQ(serialise(document), before)
        << "a restored track came back in the wrong place or without its clips";
}

TEST(Command, MoveClipUndoesAcrossTracks) {
    Document document = make_document();
    CommandStack stack;
    must_execute(stack, document, rf::timeline::make_add_track(TrackKind::video, "V1"));
    must_execute(stack, document, rf::timeline::make_add_track(TrackKind::video, "V2"));
    const TrackId first = document.tracks()[0].id;
    const TrackId second = document.tracks()[1].id;
    must_execute(stack, document, rf::timeline::make_add_clip(first, "a.mp4", 0, 100, 100, kSourceTicks));
    const ClipId clip = document.tracks()[0].clips.front().id;

    const std::string before = serialise(document);
    must_execute(stack, document, rf::timeline::make_move_clip(clip, second, 900));
    EXPECT_EQ(document.track_of_clip(clip)->id, second);

    ASSERT_TRUE(stack.undo(document).has_value());
    EXPECT_EQ(serialise(document), before);
    EXPECT_EQ(document.track_of_clip(clip)->id, first);
}

TEST(Command, TrimUndoesToTheOriginalBounds) {
    Document document = make_document();
    CommandStack stack;
    must_execute(stack, document, rf::timeline::make_add_track(TrackKind::video, "V1"));
    const TrackId track = document.tracks().front().id;
    must_execute(stack, document, rf::timeline::make_add_clip(track, "a.mp4", 100, 0, 500, kSourceTicks));
    const ClipId clip = document.tracks().front().clips.front().id;

    const std::string before = serialise(document);
    must_execute(stack, document, rf::timeline::make_set_clip_bounds(clip, 200, 100, 300));
    ASSERT_TRUE(stack.undo(document).has_value());
    EXPECT_EQ(serialise(document), before);
}

TEST(Command, SettingAFlagToItsCurrentValueUndoesToTheSameValue) {
    // The inverse records the previous value rather than flipping. If it
    // flipped, a no-op edit would undo into a *change*.
    Document document = make_document();
    CommandStack stack;
    must_execute(stack, document, rf::timeline::make_add_track(TrackKind::video, "V1"));
    const TrackId track = document.tracks().front().id;

    const std::string before = serialise(document);
    EXPECT_FALSE(document.find_track(track)->muted);

    must_execute(stack, document, rf::timeline::make_set_track_muted(track, false));
    ASSERT_TRUE(stack.undo(document).has_value());
    EXPECT_EQ(serialise(document), before);
    EXPECT_FALSE(document.find_track(track)->muted);
}

TEST(Command, RedoAfterUndoReusesTheSameIds) {
    // A redo that mints fresh ids yields a document that looks right and is not
    // the one the user undid.
    Document document = make_document();
    CommandStack stack;
    must_execute(stack, document, rf::timeline::make_add_track(TrackKind::video, "V1"));
    const TrackId track = document.tracks().front().id;
    must_execute(stack, document, rf::timeline::make_add_clip(track, "a.mp4", 0, 0, 100, kSourceTicks));

    const std::string after_edits = serialise(document);
    const ClipId original = document.tracks().front().clips.front().id;

    ASSERT_TRUE(stack.undo(document).has_value());
    ASSERT_TRUE(stack.redo(document).has_value());

    EXPECT_EQ(serialise(document), after_edits);
    EXPECT_EQ(document.tracks().front().clips.front().id, original) << "redo issued a new id";
}

TEST(Command, DeepHistoryUndoesInReverseOrder) {
    Document document = make_document();
    CommandStack stack;
    must_execute(stack, document, rf::timeline::make_add_track(TrackKind::video, "V1"));
    const TrackId track = document.tracks().front().id;
    const std::string before = serialise(document);

    for (int i = 0; i < 50; ++i) {
        must_execute(stack, document,
                     rf::timeline::make_add_clip(track, "c" + std::to_string(i) + ".mp4", 0,
                                                 i * 200, 100, kSourceTicks));
    }
    EXPECT_EQ(document.clip_count(), 50u);

    // Undo the 50 clip additions only, leaving the track that `before` captured.
    for (int i = 0; i < 50; ++i) {
        ASSERT_TRUE(stack.undo(document).has_value()) << "undo " << i;
    }
    EXPECT_EQ(serialise(document), before);
    EXPECT_EQ(stack.undo_depth(), 1u) << "the add-track command should remain";

    // And undoing that last one lands on a pristine document, id counter reset.
    ASSERT_TRUE(stack.undo(document).has_value());
    EXPECT_EQ(serialise(document),
              "reelforge/5\n"
              "timebase 1/90000\n"
              "framerate 30/1\n"
              "nextid 1\n");
}

}  // namespace
