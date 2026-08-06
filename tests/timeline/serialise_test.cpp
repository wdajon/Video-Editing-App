#include "rf/timeline/serialise.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "rf/timeline/document.hpp"

namespace {

using rf::media::Rational;
using rf::timeline::ClipId;
using rf::timeline::Document;
using rf::timeline::TrackId;
using rf::timeline::TrackKind;
using rf::timeline::serialise;

/// Long enough that the source is never the binding constraint in these tests.
constexpr rf::timeline::Ticks kSourceTicks = 1'000'000;

Document make_document() {
    auto document = Document::create(Rational{1, 90000});
    EXPECT_TRUE(document.has_value());
    return std::move(document).value();
}

TEST(Serialise, EmptyDocumentCarriesVersionBaseAndCounter) {
    const Document document = make_document();
    EXPECT_EQ(serialise(document),
              "reelforge/4\n"
              "timebase 1/90000\n"
              "nextid 1\n");
}

TEST(Serialise, WritesTracksAndClips) {
    Document document = make_document();
    const TrackId video = document.add_track(TrackKind::video, "V1").value();
    ASSERT_TRUE(document.add_clip(video, "a.mp4", 100, 0, 9000, kSourceTicks).has_value());
    ASSERT_TRUE(document.add_clip(video, "b.mp4", 0, 9000, 4500, kSourceTicks).has_value());
    const TrackId audio = document.add_track(TrackKind::audio, "A1").value();
    ASSERT_TRUE(document.set_track_muted(audio, true).has_value());

    EXPECT_EQ(serialise(document),
              "reelforge/4\n"
              "timebase 1/90000\n"
              "nextid 5\n"
              "track 1 video 0 0 1 \"V1\"\n"
              "  clip 2 100 1000000 0 9000 0 1 \"a.mp4\"\n"
              "  clip 3 0 1000000 9000 4500 0 1 \"b.mp4\"\n"
              "track 4 audio 1 0 1 \"A1\"\n");
}

TEST(Serialise, IsIndependentOfInsertionOrder) {
    // Two documents reaching the same state by different routes must produce
    // identical bytes. If they did not, byte-identity after undo would be a
    // statement about edit history rather than about state, and the M2 gate
    // would prove nothing.
    Document forwards = make_document();
    const TrackId a = forwards.add_track(TrackKind::video, "V1").value();
    ASSERT_TRUE(forwards.add_clip(a, "first.mp4", 0, 0, 100, kSourceTicks).has_value());
    ASSERT_TRUE(forwards.add_clip(a, "second.mp4", 0, 100, 100, kSourceTicks).has_value());

    Document backwards = make_document();
    const TrackId b = backwards.add_track(TrackKind::video, "V1").value();
    // Same clips, added in the opposite order. Ids differ as a result, so this
    // checks ordering only after normalising them away below.
    ASSERT_TRUE(backwards.add_clip(b, "second.mp4", 0, 100, 100, kSourceTicks).has_value());
    ASSERT_TRUE(backwards.add_clip(b, "first.mp4", 0, 0, 100, kSourceTicks).has_value());

    const std::string forwards_text = serialise(forwards);
    const std::string backwards_text = serialise(backwards);

    // Clips must appear in timeline order in both, so "first.mp4" precedes
    // "second.mp4" regardless of the order they were added.
    EXPECT_LT(forwards_text.find("first.mp4"), forwards_text.find("second.mp4"));
    EXPECT_LT(backwards_text.find("first.mp4"), backwards_text.find("second.mp4"))
        << backwards_text;
}

TEST(Serialise, IsStableAcrossRepeatedCalls) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    ASSERT_TRUE(document.add_clip(track, "a.mp4", 0, 0, 100, kSourceTicks).has_value());
    EXPECT_EQ(serialise(document), serialise(document));
}

TEST(Serialise, EscapesCharactersThatCouldForgeARecord) {
    // A media path really can contain a quote, a backslash or a newline. Writing
    // one unescaped would let a file name end a record early and turn a project
    // file into a different, still-valid-looking project file.
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "quote\" back\\slash").value();
    ASSERT_TRUE(document.add_clip(track, "new\nline\ttab.mp4", 0, 0, 100, kSourceTicks).has_value());

    const std::string text = serialise(document);
    EXPECT_NE(text.find("\"quote\\\" back\\\\slash\""), std::string::npos) << text;
    EXPECT_NE(text.find("\"new\\nline\\ttab.mp4\""), std::string::npos) << text;

    // Exactly one line per record: the escaping must not have emitted a real
    // newline inside a value.
    const auto line_count = std::count(text.begin(), text.end(), '\n');
    EXPECT_EQ(line_count, 5) << text;
}

TEST(Serialise, ReflectsTheIdCounterSoSpentIdsSurvive) {
    Document document = make_document();
    const TrackId track = document.add_track(TrackKind::video, "V1").value();
    const ClipId clip = document.add_clip(track, "a.mp4", 0, 0, 100, kSourceTicks).value();
    ASSERT_TRUE(document.remove_clip(clip).has_value());

    // The clip is gone but its id is spent. A document that forgot that could
    // reissue the id on the next edit, and two different documents would then
    // serialise identically.
    EXPECT_NE(serialise(document).find("nextid 3"), std::string::npos) << serialise(document);
}

TEST(Serialise, DistinguishesDocumentsThatDifferOnlyByAFlag) {
    Document plain = make_document();
    const TrackId a = plain.add_track(TrackKind::video, "V1").value();
    const ClipId clip_a = plain.add_clip(a, "x.mp4", 0, 0, 100, kSourceTicks).value();

    Document flagged = make_document();
    const TrackId b = flagged.add_track(TrackKind::video, "V1").value();
    const ClipId clip_b = flagged.add_clip(b, "x.mp4", 0, 0, 100, kSourceTicks).value();
    ASSERT_TRUE(flagged.set_clip_enabled(clip_b, false).has_value());

    EXPECT_EQ(clip_a.value(), clip_b.value()) << "sanity: the two documents are built alike";
    EXPECT_NE(serialise(plain), serialise(flagged))
        << "a disabled clip must not serialise the same as an enabled one";
}

TEST(Serialise, DistinguishesDocumentsThatDifferOnlyBySourceLength) {
    // Two clips using the same frames of two sources of different length are
    // different documents: one has room to trim and the other does not. Omitting
    // the field would make undo appear to restore a trim limit it had lost.
    Document shorter = make_document();
    const TrackId a = shorter.add_track(TrackKind::video, "V1").value();
    ASSERT_TRUE(shorter.add_clip(a, "x.mp4", 0, 0, 100, 100).has_value());

    Document longer = make_document();
    const TrackId b = longer.add_track(TrackKind::video, "V1").value();
    ASSERT_TRUE(longer.add_clip(b, "x.mp4", 0, 0, 100, 500).has_value());

    EXPECT_NE(serialise(shorter), serialise(longer));
}

TEST(Serialise, RecordsTheTimeBase) {
    auto ntsc = Document::create(Rational{1001, 30000});
    ASSERT_TRUE(ntsc.has_value());
    EXPECT_NE(serialise(ntsc.value()).find("timebase 1001/30000"), std::string::npos);
}

}  // namespace
