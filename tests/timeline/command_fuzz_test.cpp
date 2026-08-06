// M2's exit gate: 10,000 random operations, then undo everything, and the
// document must be byte-identical to where it started.
//
// Two things make this a real check rather than a ritual:
//
//  * It asserts byte equality AND structural equality. Byte equality alone
//    would keep passing if the serialiser omitted a field, because both sides
//    would omit it identically -- undo could silently lose that field forever.
//    Structural equality alone would miss ordering and id-counter drift, which
//    serialise but compare equal member-by-member.
//
//  * Operations are generated against the document's actual contents, so most
//    of them succeed. A fuzz that mostly generates rejected edits exercises
//    the validation and never the inverses, and would pass with every undo
//    path broken.

#include <gtest/gtest.h>

#include <cstdint>
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
using rf::timeline::serialise;

/// Long enough that the source is never the binding constraint here. This fuzz
/// is about the command stack; the media limit from ADR 009 has its own tests.
constexpr Ticks kSourceTicks = 1'000'000;

struct Counters {
    int attempted = 0;
    int applied = 0;
    int rejected = 0;
};

/// Builds one random command aimed at whatever the document currently holds.
std::unique_ptr<rf::timeline::Command> random_command(const Document& document,
                                                      std::mt19937_64& random) {
    std::vector<TrackId> tracks;
    std::vector<ClipId> clips;
    for (const Track& track : document.tracks()) {
        tracks.push_back(track.id);
        for (const Clip& clip : track.clips) {
            clips.push_back(clip.id);
        }
    }

    const auto pick_track = [&] { return tracks[random() % tracks.size()]; };
    const auto pick_clip = [&] { return clips[random() % clips.size()]; };
    const auto pick_ticks = [&] { return static_cast<Ticks>(random() % 4000) * 10; };

    // Without tracks nothing else is possible, so bias hard toward creating one.
    if (tracks.empty() || random() % 100 < 4) {
        const TrackKind kind = (random() % 2) == 0 ? TrackKind::video : TrackKind::audio;
        return rf::timeline::make_add_track(kind, "T" + std::to_string(random() % 1000));
    }

    const std::uint64_t choice = random() % 100;

    if (clips.empty() || choice < 34) {
        const Ticks start = pick_ticks();
        const Ticks duration = static_cast<Ticks>(1 + (random() % 2000));
        return rf::timeline::make_add_clip(pick_track(), "m" + std::to_string(random() % 50) + ".mp4",
                                           static_cast<Ticks>(random() % 5000), start, duration,
                                           kSourceTicks);
    }
    if (choice < 50) {
        return rf::timeline::make_remove_clip(pick_clip());
    }
    if (choice < 68) {
        return rf::timeline::make_move_clip(pick_clip(), pick_track(), pick_ticks());
    }
    if (choice < 82) {
        return rf::timeline::make_set_clip_bounds(pick_clip(), static_cast<Ticks>(random() % 5000),
                                                  pick_ticks(),
                                                  static_cast<Ticks>(1 + (random() % 2000)));
    }
    if (choice < 88) {
        return rf::timeline::make_set_clip_enabled(pick_clip(), (random() % 2) == 0);
    }
    if (choice < 92) {
        return rf::timeline::make_set_track_muted(pick_track(), (random() % 2) == 0);
    }
    if (choice < 96) {
        return rf::timeline::make_set_track_locked(pick_track(), (random() % 2) == 0);
    }
    return rf::timeline::make_remove_track(pick_track());
}

/// Runs `operations` random edits, then undoes all of them, and checks the
/// document came back exactly.
void run_fuzz(std::uint64_t seed, int operations, Counters& counters) {
    auto created = Document::create(Rational{1, 90000});
    ASSERT_TRUE(created.has_value());
    Document document = std::move(created).value();

    const std::string initial_bytes = serialise(document);
    const Document initial_document = document;

    CommandStack stack;
    std::mt19937_64 random{seed};

    for (int i = 0; i < operations; ++i) {
        ++counters.attempted;
        auto command = random_command(document, random);
        ASSERT_NE(command, nullptr);
        if (stack.execute(document, std::move(command)).has_value()) {
            ++counters.applied;
        } else {
            ++counters.rejected;
        }
    }

    // A rejected edit must never have reached the history.
    ASSERT_EQ(stack.undo_depth(), static_cast<std::size_t>(counters.applied))
        << "seed " << seed << ": the undo stack does not match the number of applied edits";

    std::size_t undone = 0;
    while (stack.can_undo()) {
        const auto result = stack.undo(document);
        ASSERT_TRUE(result.has_value())
            << "seed " << seed << ": undo " << undone << " failed: " << result.error().to_string();
        ++undone;
    }

    EXPECT_EQ(undone, static_cast<std::size_t>(counters.applied));
    EXPECT_EQ(document, initial_document) << "seed " << seed << ": structural state differs";
    EXPECT_EQ(serialise(document), initial_bytes) << "seed " << seed << ": bytes differ";
    EXPECT_EQ(document.next_id(), initial_document.next_id())
        << "seed " << seed << ": the id counter drifted";
}

TEST(CommandFuzz, TenThousandOperationsUndoToAByteIdenticalDocument) {
    Counters counters;
    run_fuzz(0x5eed'0001u, 10000, counters);

    // If almost everything were rejected, the inverses would barely be
    // exercised and this test would pass while undo was broken.
    EXPECT_GT(counters.applied, counters.attempted / 2)
        << "too few edits applied (" << counters.applied << " of " << counters.attempted
        << "); the fuzz is testing validation, not undo";
    std::printf("[fuzz] attempted=%d applied=%d rejected=%d\n", counters.attempted,
                counters.applied, counters.rejected);
}

TEST(CommandFuzz, ManySeedsAllUndoCleanly) {
    // One seed exercises one path through the state space. Several shorter runs
    // reach shapes a single long run never visits.
    for (std::uint64_t seed = 1; seed <= 25; ++seed) {
        Counters counters;
        run_fuzz(seed * 7919u, 400, counters);
        if (::testing::Test::HasFatalFailure()) {
            return;
        }
    }
}

TEST(CommandFuzz, RedoReproducesTheSameDocumentIdsIncluded) {
    // Undo then redo must land on exactly the document that was undone. A redo
    // that issues fresh ids produces something that looks right and is not.
    auto created = Document::create(Rational{1, 90000});
    ASSERT_TRUE(created.has_value());
    Document document = std::move(created).value();

    CommandStack stack;
    std::mt19937_64 random{0xd0'd0'd0u};
    for (int i = 0; i < 500; ++i) {
        auto command = random_command(document, random);
        (void)stack.execute(document, std::move(command));
    }

    const std::string after_edits = serialise(document);
    const std::size_t depth = stack.undo_depth();

    while (stack.can_undo()) {
        ASSERT_TRUE(stack.undo(document).has_value());
    }
    ASSERT_EQ(stack.redo_depth(), depth);

    while (stack.can_redo()) {
        const auto result = stack.redo(document);
        ASSERT_TRUE(result.has_value()) << result.error().to_string();
    }

    EXPECT_EQ(serialise(document), after_edits) << "redo did not reproduce the edited document";
    EXPECT_EQ(stack.undo_depth(), depth);
}

TEST(CommandFuzz, UndoRedoCyclesAreStable) {
    // Repeated undo/redo of the same history must not accumulate drift.
    auto created = Document::create(Rational{1, 90000});
    ASSERT_TRUE(created.has_value());
    Document document = std::move(created).value();

    CommandStack stack;
    std::mt19937_64 random{0xabc'123u};
    for (int i = 0; i < 200; ++i) {
        (void)stack.execute(document, random_command(document, random));
    }
    const std::string reference = serialise(document);

    for (int cycle = 0; cycle < 5; ++cycle) {
        while (stack.can_undo()) {
            ASSERT_TRUE(stack.undo(document).has_value()) << "cycle " << cycle;
        }
        while (stack.can_redo()) {
            ASSERT_TRUE(stack.redo(document).has_value()) << "cycle " << cycle;
        }
        ASSERT_EQ(serialise(document), reference) << "drift after cycle " << cycle;
    }
}

}  // namespace
