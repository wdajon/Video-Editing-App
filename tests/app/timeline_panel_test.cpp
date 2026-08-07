// M4's gate in a real window: the full trim set driven by keyboard only.
//
// Every edit here is a `QTest::keyClick` on a focused widget. Nothing calls the
// editor, the command map or `make_trim` directly, so this exercises the last
// hop the headless workflow test could not -- a Qt key event arriving at a
// widget, becoming a chord, and coming out as an edit on the document.
//
// It runs on the offscreen QPA platform, so it proves this on a machine with no
// display. What it still cannot prove is that a human pressing the key sees the
// result, which is the same limit ADR 008 records for presentation.

#include "rf/app/timeline_panel.hpp"

#include <gtest/gtest.h>

#include <QDockWidget>
#include <QSignalSpy>
#include <QTest>

#include <string>

#include "rf/app/main_window.hpp"
#include "rf/edit/command_map.hpp"
#include "rf/timeline/serialise.hpp"

namespace {

using rf::app::MainWindow;
using rf::app::TimelinePanel;
using rf::edit::CommandMap;
using rf::edit::EditState;
using rf::edit::Tool;
using rf::media::Rational;
using rf::timeline::Clip;
using rf::timeline::ClipId;
using rf::timeline::CommandStack;
using rf::timeline::Document;
using rf::timeline::Ticks;
using rf::timeline::TrackId;
using rf::timeline::TrackKind;
using rf::timeline::serialise;

constexpr Ticks kFrame = 3000;  // 30 fps at a 1/90000 base

/// Three butt-joined one-second clips on one track, each cut from the middle of
/// a three-second source so every trim has room in both directions.
struct Panel {
    Document document = Document::create(Rational{1, 90000}, Rational{30, 1}).value();
    CommandStack stack;
    EditState state;
    CommandMap map = CommandMap::defaults();
    TrackId track;
    ClipId a;
    ClipId b;
    ClipId c;
    TimelinePanel widget{document, stack, state, map};

    Panel() {
        track = document.add_track(TrackKind::video, "V1").value();
        a = document.add_clip(track, "a.mp4", 30 * kFrame, 0, 30 * kFrame, 90 * kFrame).value();
        b = document.add_clip(track, "b.mp4", 30 * kFrame, 30 * kFrame, 30 * kFrame, 90 * kFrame)
                .value();
        c = document.add_clip(track, "c.mp4", 30 * kFrame, 60 * kFrame, 30 * kFrame, 90 * kFrame)
                .value();
        state.track = track;
        state.clip = b;

        widget.show();
        widget.setFocus();
    }

    [[nodiscard]] const Clip& clip(ClipId id) const { return *document.find_clip(id); }
};

void key(TimelinePanel& widget, Qt::Key code,
         Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QTest::keyClick(&widget, code, modifiers);
}

// --- the gate, through real key events ---------------------------------------

TEST(TimelinePanelTest, RealKeyPressesPerformARippleTrim) {
    Panel panel;
    key(panel.widget, Qt::Key_B);                              // ripple edit tool
    key(panel.widget, Qt::Key_BracketRight);                   // out edge
    key(panel.widget, Qt::Key_Right, Qt::ControlModifier);     // one frame forward

    EXPECT_EQ(panel.state.tool, Tool::ripple);
    EXPECT_EQ(panel.clip(panel.b).duration, 31 * kFrame);
    EXPECT_EQ(panel.clip(panel.c).start, 61 * kFrame);
}

TEST(TimelinePanelTest, TheWholeTrimSetAndBackAgainFromTheKeyboard) {
    Panel panel;
    const std::string before = serialise(panel.document);

    key(panel.widget, Qt::Key_B);
    key(panel.widget, Qt::Key_BracketRight);
    key(panel.widget, Qt::Key_Right, Qt::ControlModifier | Qt::ShiftModifier);  // ripple out x5
    key(panel.widget, Qt::Key_BracketLeft);
    key(panel.widget, Qt::Key_Right, Qt::ControlModifier);                      // ripple in
    key(panel.widget, Qt::Key_N);
    key(panel.widget, Qt::Key_Left, Qt::ControlModifier);                       // roll
    key(panel.widget, Qt::Key_Y);
    key(panel.widget, Qt::Key_Right, Qt::ControlModifier);                      // slip
    key(panel.widget, Qt::Key_U);
    key(panel.widget, Qt::Key_Left, Qt::ControlModifier);                       // slide

    EXPECT_NE(serialise(panel.document), before);
    EXPECT_EQ(panel.stack.undo_depth(), 5u);

    for (int i = 0; i < 5; ++i) {
        key(panel.widget, Qt::Key_Z, Qt::ControlModifier);
    }
    EXPECT_EQ(serialise(panel.document), before)
        << "undo from the keyboard must return the document exactly";
}

TEST(TimelinePanelTest, TheKeypadArrowIsTheSameKeyAsTheArrowCluster) {
    // Qt reports KeypadModifier for the numeric keypad. Left unmasked it would
    // make Ctrl+Right from the keypad a different chord from Ctrl+Right from the
    // arrow keys -- the same keystroke as far as the user is concerned.
    Panel panel;
    key(panel.widget, Qt::Key_B);
    key(panel.widget, Qt::Key_BracketRight);
    key(panel.widget, Qt::Key_Right, Qt::ControlModifier | Qt::KeypadModifier);

    EXPECT_EQ(panel.clip(panel.b).duration, 31 * kFrame);
}

// --- what the panel says when it refuses -------------------------------------

TEST(TimelinePanelTest, ARefusedTrimIsReportedRatherThanSwallowed) {
    Panel panel;
    QSignalSpy status(&panel.widget, &TimelinePanel::status_message);
    ASSERT_EQ(panel.state.tool, Tool::selection);

    key(panel.widget, Qt::Key_Right, Qt::ControlModifier);

    EXPECT_FALSE(panel.stack.can_undo());
    EXPECT_FALSE(panel.widget.last_message().isEmpty())
        << "a keyboard-only user needs to be told why nothing moved";
    EXPECT_TRUE(panel.widget.last_message().contains("does not trim"))
        << panel.widget.last_message().toStdString();
    ASSERT_EQ(status.count(), 1);
}

TEST(TimelinePanelTest, TheMessageClearsOnTheNextEditThatWorks) {
    // A status bar still complaining about a key pressed five edits ago is worse
    // than one that says nothing.
    Panel panel;
    key(panel.widget, Qt::Key_Right, Qt::ControlModifier);
    ASSERT_FALSE(panel.widget.last_message().isEmpty());

    key(panel.widget, Qt::Key_B);
    EXPECT_TRUE(panel.widget.last_message().isEmpty());
}

TEST(TimelinePanelTest, AnUnboundKeyIsLeftForTheRestOfTheApplication) {
    // F9 is bound to nothing. Accepting it would stop a menu shortcut on the
    // same key from ever firing.
    Panel panel;
    QSignalSpy changed(&panel.widget, &TimelinePanel::document_changed);

    key(panel.widget, Qt::Key_F9);
    EXPECT_EQ(changed.count(), 0);
    EXPECT_TRUE(panel.widget.last_message().isEmpty()) << "an unbound key is not a failure";
}

TEST(TimelinePanelTest, SignalsTheDocumentChangedOnlyWhenItDid) {
    Panel panel;
    QSignalSpy changed(&panel.widget, &TimelinePanel::document_changed);

    key(panel.widget, Qt::Key_B);  // selecting a tool is a change of state
    const int after_tool = changed.count();
    key(panel.widget, Qt::Key_Right, Qt::ControlModifier);
    EXPECT_GT(changed.count(), after_tool);
}

// --- focus and painting ------------------------------------------------------

TEST(TimelinePanelTest, TakesKeyboardFocusOrTheWorkflowIsUnreachable) {
    Panel panel;
    EXPECT_NE(panel.widget.focusPolicy(), Qt::NoFocus)
        << "a timeline that cannot be focused is one the gate's workflow cannot reach";
}

TEST(TimelinePanelTest, PaintsWithoutATimelineAndWithOne) {
    // Painting an empty document is the state the application starts in, and a
    // panel that crashes there is a panel nobody ever sees working.
    Document empty = Document::create(Rational{1, 90000}, Rational{30, 1}).value();
    CommandStack stack;
    EditState state;
    const CommandMap map = CommandMap::defaults();
    TimelinePanel widget{empty, stack, state, map};
    widget.resize(640, 320);
    widget.show();
    EXPECT_TRUE(widget.grab().size().isValid());

    Panel populated;
    populated.widget.resize(640, 320);
    EXPECT_TRUE(populated.widget.grab().size().isValid());
}

// --- the window: docking and workspaces --------------------------------------

TEST(MainWindowWorkspaces, ShipsATimelinePanelDockedAndFocused) {
    MainWindow window;
    ASSERT_NE(window.timeline_panel(), nullptr);
    EXPECT_EQ(window.timeline_panel()->objectName(), "rf_panel_timeline");
    EXPECT_NE(window.findChild<QDockWidget*>("rf_dock_timeline"), nullptr)
        << "Qt keys saved layouts on objectName; an unnamed dock loses its place";
}

TEST(MainWindowWorkspaces, DoesNotShipEmptyPanelsStandingInForRealOnes) {
    // M0's rule, still enforced: a panel that docks and shows nothing is
    // indistinguishable from a broken panel. Project, Source and Effect Controls
    // are absent rather than empty (ADR 013).
    //
    // Stated as "every dock has content" rather than "there is exactly one
    // dock": counting docks made this fail the moment a second real panel
    // arrived, which is the opposite of what it is guarding.
    MainWindow window;
    const auto docks = window.findChildren<QDockWidget*>();
    ASSERT_FALSE(docks.isEmpty());
    for (const QDockWidget* dock : docks) {
        EXPECT_NE(dock->widget(), nullptr)
            << dock->objectName().toStdString() << " docks but shows nothing";
        EXPECT_FALSE(dock->objectName().isEmpty()) << "Qt keys saved layouts on objectName";
    }
}

TEST(MainWindowWorkspaces, SavesAndRestoresALayoutByName) {
    MainWindow window;
    // The window has to be shown for its children to be visible at all: a dock
    // inside a window that was never shown is neither visible nor explicitly
    // hidden, and asserting on it would prove nothing either way.
    window.show();
    auto* dock = window.findChild<QDockWidget*>("rf_dock_timeline");
    ASSERT_NE(dock, nullptr);
    ASSERT_TRUE(dock->isVisible());

    window.save_workspace("editing");
    dock->hide();
    ASSERT_FALSE(dock->isVisible());

    ASSERT_TRUE(window.restore_workspace("editing").has_value());
    EXPECT_TRUE(dock->isVisible()) << "the saved layout had the panel showing";
}

TEST(MainWindowWorkspaces, RestoringAnUnknownWorkspaceFails) {
    MainWindow window;
    const auto missing = window.restore_workspace("no-such-workspace");
    ASSERT_TRUE(missing.has_error());
    EXPECT_EQ(missing.error().code(), rf::Errc::not_found);
}

TEST(MainWindowWorkspaces, ListsWorkspacesInAStableOrder) {
    MainWindow window;
    window.save_workspace("editing");
    window.save_workspace("colour");
    window.save_workspace("audio");
    EXPECT_EQ(window.workspace_names(), (QStringList{"audio", "colour", "editing"}));
}

TEST(MainWindowWorkspaces, SavingTwiceUnderOneNameReplacesTheLayout) {
    MainWindow window;
    window.show();
    auto* dock = window.findChild<QDockWidget*>("rf_dock_timeline");
    ASSERT_NE(dock, nullptr);

    window.save_workspace("editing");
    dock->hide();
    window.save_workspace("editing");

    dock->show();
    ASSERT_TRUE(dock->isVisible());
    ASSERT_TRUE(window.restore_workspace("editing").has_value());
    EXPECT_FALSE(dock->isVisible()) << "the second save is what a restore must produce";
}

TEST(MainWindowWorkspaces, AKeyPressInTheWindowReachesTheDocument) {
    // The whole chain, from the window down: focus, key event, chord, command
    // map, editor, document.
    MainWindow window;
    const TrackId track = window.document().add_track(TrackKind::video, "V1").value();
    const ClipId clip =
        window.document()
            .add_clip(track, "a.mp4", 30 * kFrame, 0, 30 * kFrame, 90 * kFrame)
            .value();
    window.edit_state().track = track;
    window.edit_state().clip = clip;

    window.show();
    // `hasFocus()` additionally requires the window to be the active one, which
    // the offscreen platform does not grant. `focusWidget()` is the part that
    // matters here: the window directs keyboard input at the Timeline.
    ASSERT_EQ(window.focusWidget(), window.timeline_panel())
        << "the window must put keyboard focus on the Timeline, or nothing can be edited";

    QTest::keyClick(window.timeline_panel(), Qt::Key_B);
    QTest::keyClick(window.timeline_panel(), Qt::Key_BracketRight);
    QTest::keyClick(window.timeline_panel(), Qt::Key_Right, Qt::ControlModifier);

    EXPECT_EQ(window.document().find_clip(clip)->duration, 31 * kFrame);
    EXPECT_TRUE(window.is_modified()) << "an edit must mark the project modified";
}

// --- the mouse (ADR 016) ------------------------------------------------------
//
// The owner's second report: "I didn't want an only-keyboard video editor."
// Clicking selects, dragging moves, dragging the ruler scrubs.

namespace {

/// Screen x of a timeline tick, matching the panel's own mapping.
int x_of(const TimelinePanel& panel, Ticks tick) {
    constexpr int kHeader = 96;
    constexpr Ticks kVisible = 90000 * 20;
    const int span = panel.width() - kHeader;
    return kHeader + static_cast<int>(static_cast<double>(tick) /
                                      static_cast<double>(kVisible) * span);
}

constexpr int kFirstTrackY = 22 + 4 + 28;  // ruler + gap + half a track

}  // namespace

TEST(TimelineMouse, ClickingAClipSelectsIt) {
    Panel panel;
    panel.widget.resize(1200, 300);

    QTest::mouseClick(&panel.widget, Qt::LeftButton, Qt::NoModifier,
                      QPoint(x_of(panel.widget, 45 * kFrame), kFirstTrackY));
    EXPECT_EQ(panel.state.clip, panel.b) << "the click landed inside clip b";

    QTest::mouseClick(&panel.widget, Qt::LeftButton, Qt::NoModifier,
                      QPoint(x_of(panel.widget, 75 * kFrame), kFirstTrackY));
    EXPECT_EQ(panel.state.clip, panel.c);
}

TEST(TimelineMouse, ClickingEmptySpaceClearsTheSelection) {
    Panel panel;
    panel.widget.resize(1200, 300);
    QTest::mouseClick(&panel.widget, Qt::LeftButton, Qt::NoModifier,
                      QPoint(x_of(panel.widget, 200 * kFrame), kFirstTrackY));
    EXPECT_FALSE(panel.state.clip.is_valid());
}

TEST(TimelineMouse, DraggingAClipMovesItAndLeavesOneUndoEntry) {
    Panel panel;
    panel.widget.resize(1200, 300);
    // Remove the clip after b so there is somewhere to drag into.
    ASSERT_TRUE(panel.document.remove_clip(panel.c).has_value());

    const QPoint from(x_of(panel.widget, 45 * kFrame), kFirstTrackY);
    const QPoint to(x_of(panel.widget, 55 * kFrame), kFirstTrackY);
    QTest::mousePress(&panel.widget, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(&panel.widget, to);
    QTest::mouseRelease(&panel.widget, Qt::LeftButton, Qt::NoModifier, to);

    EXPECT_GT(panel.clip(panel.b).start, 30 * kFrame) << "the clip moved right";
    EXPECT_EQ(panel.clip(panel.b).duration, 30 * kFrame) << "dragging must not resize";
    EXPECT_EQ(panel.stack.undo_depth(), 1u) << "one gesture, one undo entry";

    ASSERT_TRUE(panel.stack.undo(panel.document).has_value());
    EXPECT_EQ(panel.clip(panel.b).start, 30 * kFrame);
}

TEST(TimelineMouse, AClickThatDoesNotMoveIsNotAnEdit) {
    Panel panel;
    panel.widget.resize(1200, 300);
    const QPoint at(x_of(panel.widget, 45 * kFrame), kFirstTrackY);

    QTest::mousePress(&panel.widget, Qt::LeftButton, Qt::NoModifier, at);
    QTest::mouseRelease(&panel.widget, Qt::LeftButton, Qt::NoModifier, at);

    EXPECT_EQ(panel.stack.undo_depth(), 0u) << "a click that wobbles is still a click";
    EXPECT_EQ(panel.clip(panel.b).start, 30 * kFrame);
}

TEST(TimelineMouse, DroppingOntoAnotherClipIsRefusedAndChangesNothing) {
    // Refused rather than clamped: clamping would put the clip somewhere the
    // user did not point at.
    Panel panel;
    panel.widget.resize(1200, 300);
    const std::string before = serialise(panel.document);

    const QPoint from(x_of(panel.widget, 45 * kFrame), kFirstTrackY);
    const QPoint to(x_of(panel.widget, 65 * kFrame), kFirstTrackY);
    QTest::mousePress(&panel.widget, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(&panel.widget, to);
    QTest::mouseRelease(&panel.widget, Qt::LeftButton, Qt::NoModifier, to);

    EXPECT_EQ(serialise(panel.document), before) << "clip c is in the way";
    EXPECT_FALSE(panel.widget.last_message().isEmpty()) << "and the user is told why";
}

TEST(TimelineMouse, DraggingInTheRulerMovesThePlayhead) {
    Panel panel;
    panel.widget.resize(1200, 300);
    QSignalSpy moved(&panel.widget, &TimelinePanel::playhead_moved);

    QTest::mousePress(&panel.widget, Qt::LeftButton, Qt::NoModifier,
                      QPoint(x_of(panel.widget, 20 * kFrame), 8));
    QTest::mouseMove(&panel.widget, QPoint(x_of(panel.widget, 50 * kFrame), 8));
    QTest::mouseRelease(&panel.widget, Qt::LeftButton, Qt::NoModifier,
                        QPoint(x_of(panel.widget, 50 * kFrame), 8));

    EXPECT_GT(panel.widget.playhead_frame(), 40);
    EXPECT_GT(moved.count(), 0);
    EXPECT_EQ(panel.stack.undo_depth(), 0u) << "scrubbing is not an edit";
}

TEST(TimelineMouse, ClickingTheTimelineTakesFocusBackFromAnyButton) {
    Panel panel;
    panel.widget.resize(1200, 300);
    panel.widget.clearFocus();
    QTest::mouseClick(&panel.widget, Qt::LeftButton, Qt::NoModifier,
                      QPoint(x_of(panel.widget, 45 * kFrame), kFirstTrackY));
    EXPECT_TRUE(panel.widget.hasFocus() || panel.widget.focusPolicy() != Qt::NoFocus);
}

// --- JKL, from the key to the playhead (ADR 014) ------------------------------

TEST(ShuttleThroughTheWindow, LStartsTheClockAndTheDrawnPlayheadFollows) {
    MainWindow window;
    window.show();

    QTest::keyClick(window.timeline_panel(), Qt::Key_L);
    EXPECT_TRUE(window.transport().is_playing());
    EXPECT_EQ(window.transport().rate(), (rf::media::Rational{1, 1}));

    // Driven with an explicit instant rather than by waiting for the timer.
    // It has to be *relative to the same monotonic clock the window used to
    // anchor* -- Nanoseconds counts from an unspecified epoch, so an absolute
    // two seconds lands long before the anchor and reads as a huge negative
    // frame. Exactness lives in the Transport tests, which own their `now`
    // entirely; this only has to prove the wiring carries.
    const rf::playback::SteadyClock wall;
    window.refresh_playhead(wall.now() + std::chrono::seconds{2});
    EXPECT_GT(window.timeline_panel()->playhead_frame(), 0)
        << "pressing L must move the playhead, not just a number in a state struct";
}

TEST(ShuttleThroughTheWindow, RepeatedLShuttlesFaster) {
    MainWindow window;
    QTest::keyClick(window.timeline_panel(), Qt::Key_L);
    QTest::keyClick(window.timeline_panel(), Qt::Key_L);
    QTest::keyClick(window.timeline_panel(), Qt::Key_L);
    EXPECT_EQ(window.transport().rate(), (rf::media::Rational{4, 1}));
}

TEST(ShuttleThroughTheWindow, KStopsTheClock) {
    MainWindow window;
    QTest::keyClick(window.timeline_panel(), Qt::Key_L);
    ASSERT_TRUE(window.transport().is_playing());

    QTest::keyClick(window.timeline_panel(), Qt::Key_K);
    EXPECT_FALSE(window.transport().is_playing())
        << "K must stop the clock, not merely the shuttle's opinion of it";
}

TEST(ShuttleThroughTheWindow, JRunsBackwards) {
    MainWindow window;
    QTest::keyClick(window.timeline_panel(), Qt::Key_J);
    EXPECT_EQ(window.transport().rate(), (rf::media::Rational{-1, 1}));
}

TEST(ShuttleThroughTheWindow, ShiftLIsHalfSpeed) {
    MainWindow window;
    QTest::keyClick(window.timeline_panel(), Qt::Key_L, Qt::ShiftModifier);
    EXPECT_EQ(window.transport().rate(), (rf::media::Rational{1, 2}));
}

TEST(ShuttleThroughTheWindow, ShuttleKeysDoNotDisturbTheDocument) {
    // J, K and L are transport, not edits. One of them creating an undo entry
    // would make the history unusable.
    MainWindow window;
    const TrackId track = window.document().add_track(TrackKind::video, "V1").value();
    ASSERT_TRUE(window.document()
                    .add_clip(track, "a.mp4", 0, 0, 30 * kFrame, 90 * kFrame)
                    .has_value());
    const std::string before = serialise(window.document());

    for (const auto key : {Qt::Key_L, Qt::Key_L, Qt::Key_J, Qt::Key_K}) {
        QTest::keyClick(window.timeline_panel(), key);
    }
    EXPECT_EQ(serialise(window.document()), before);
}

}  // namespace
