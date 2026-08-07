// The palette exists because a user could not tell whether a key had worked.
// So what these assert is mostly *visibility*: that a button says which key it
// is, that the palette shows which tool is live, and that clicking and pressing
// reach the same code.

#include "rf/app/tool_palette.hpp"

#include <gtest/gtest.h>

#include <QAction>
#include <QMenu>
#include <QTest>
#include <QToolButton>

#include "rf/app/demo_timeline.hpp"
#include "rf/app/main_window.hpp"
#include "rf/app/timeline_panel.hpp"
#include "rf/edit/command_map.hpp"
#include "rf/timeline/serialise.hpp"

namespace {

using rf::app::MainWindow;
using rf::app::ToolPalette;
using rf::app::build_demo_timeline;
using rf::edit::Action;
using rf::edit::CommandMap;
using rf::edit::Tool;

/// A window with something selected, as `--demo-timeline` produces.
MainWindow* make_loaded_window() {
    auto* window = new MainWindow;
    EXPECT_TRUE(build_demo_timeline(window->document()).has_value());
    window->edit_state().track = window->document().tracks().front().id;
    window->edit_state().clip = window->document().tracks().front().clips[1].id;
    return window;
}

TEST(ToolPaletteTest, EveryToolShowsItsOwnShortcut) {
    // Read from the live command map, never written beside the button, so a
    // remapped key relabels itself and nothing can advertise a key that does
    // nothing.
    const CommandMap map = CommandMap::defaults();
    const ToolPalette palette{map};

    EXPECT_TRUE(palette.label_for(Action::select_tool_selection).contains("V"));
    EXPECT_TRUE(palette.label_for(Action::select_tool_ripple).contains("B"));
    EXPECT_TRUE(palette.label_for(Action::select_tool_roll).contains("N"));
    EXPECT_TRUE(palette.label_for(Action::select_tool_slip).contains("Y"));
    EXPECT_TRUE(palette.label_for(Action::select_tool_slide).contains("U"));
}

TEST(ToolPaletteTest, IsANarrowStripRatherThanAWallOfButtons) {
    // The defect this replaced: a full-width column of every command that
    // expanded to fill the window and pushed the Timeline out of sight.
    const CommandMap map = CommandMap::defaults();
    const ToolPalette palette{map};
    EXPECT_LT(palette.width(), 60) << "the strip must not be able to swallow the window";
    EXPECT_EQ(palette.sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);
}

TEST(ToolPaletteTest, HoldsOnlyToolsAndOnlyOnesThatDoSomething) {
    // Razor, pen, hand, zoom and type are absent rather than present and dead:
    // M0's rule about panels that show nothing applies to buttons too.
    const CommandMap map = CommandMap::defaults();
    const ToolPalette palette{map};
    const auto buttons = palette.findChildren<QToolButton*>();
    EXPECT_EQ(buttons.size(), 3) << "selection, the ripple/roll slot, the slip/slide slot";

    for (const Action command :
         {Action::trim_forward, Action::nudge_forward, Action::undo, Action::shuttle_forward}) {
        EXPECT_EQ(palette.button_for(command), nullptr)
            << rf::edit::to_string(command) << " is a command, not a tool";
    }
}

TEST(ToolPaletteTest, ToolsThatShareASlotShareAButtonAndAFlyout) {
    // Premiere's grouping, and the behaviour that was asked for: click to use,
    // click and hold to change which tool the slot holds.
    const CommandMap map = CommandMap::defaults();
    const ToolPalette palette{map};

    EXPECT_EQ(palette.button_for(Action::select_tool_ripple),
              palette.button_for(Action::select_tool_roll));
    EXPECT_EQ(palette.button_for(Action::select_tool_slip),
              palette.button_for(Action::select_tool_slide));
    EXPECT_NE(palette.button_for(Action::select_tool_ripple),
              palette.button_for(Action::select_tool_slip));

    QToolButton* shared = palette.button_for(Action::select_tool_ripple);
    ASSERT_NE(shared, nullptr);
    ASSERT_NE(shared->menu(), nullptr) << "a shared slot needs a flyout to choose from";
    EXPECT_EQ(shared->popupMode(), QToolButton::DelayedPopup)
        << "click uses the tool; click and hold opens the flyout";
    EXPECT_EQ(shared->menu()->actions().size(), 2);

    // The Selection tool is alone in its slot, so it has no flyout.
    EXPECT_EQ(palette.button_for(Action::select_tool_selection)->menu(), nullptr);
}

TEST(ToolPaletteTest, ChoosingFromTheFlyoutSelectsThatToolAndLeavesItShowing) {
    MainWindow* window = make_loaded_window();
    QAction* rolling = window->tool_palette()->menu_action_for(Action::select_tool_roll);
    ASSERT_NE(rolling, nullptr);

    rolling->trigger();
    EXPECT_EQ(window->edit_state().tool, rf::edit::Tool::roll);

    // The slot now holds rolling, so a plain click uses it rather than ripple.
    QTest::keyClick(window->timeline_panel(), Qt::Key_V);
    ASSERT_EQ(window->edit_state().tool, rf::edit::Tool::selection);
    window->tool_palette()->button_for(Action::select_tool_roll)->click();
    EXPECT_EQ(window->edit_state().tool, rf::edit::Tool::roll)
        << "the slot kept the tool chosen from the flyout";
    delete window;
}

TEST(ToolPaletteTest, PressingASiblingsKeyMakesTheSlotShowIt) {
    // Otherwise the strip would claim ripple is active while displaying the
    // rolling icon.
    MainWindow* window = make_loaded_window();
    QToolButton* slot = window->tool_palette()->button_for(Action::select_tool_ripple);

    QTest::keyClick(window->timeline_panel(), Qt::Key_N);
    ASSERT_EQ(window->edit_state().tool, rf::edit::Tool::roll);
    EXPECT_TRUE(slot->isChecked());
    EXPECT_EQ(slot->property("rf_tool").toInt(), static_cast<int>(Action::select_tool_roll));

    QTest::keyClick(window->timeline_panel(), Qt::Key_B);
    EXPECT_EQ(slot->property("rf_tool").toInt(), static_cast<int>(Action::select_tool_ripple));
    delete window;
}

// --- the commands live in menus now -------------------------------------------

TEST(MainWindowMenus, CarryEveryCommandThatIsNotATool) {
    MainWindow window;
    for (const Action action :
         {Action::nudge_backward, Action::nudge_forward, Action::slip_backward,
          Action::slip_forward, Action::slide_backward, Action::slide_forward,
          Action::select_in_edge, Action::select_out_edge, Action::trim_backward,
          Action::trim_forward, Action::trim_backward_many, Action::trim_forward_many,
          Action::select_previous_clip, Action::select_next_clip,
          Action::select_previous_track, Action::select_next_track, Action::play_stop,
          Action::step_backward, Action::step_forward, Action::shuttle_backward,
          Action::shuttle_stop, Action::shuttle_forward, Action::undo, Action::redo}) {
        EXPECT_NE(window.menu_action_for(action), nullptr)
            << rf::edit::to_string(action) << " is not reachable with a mouse";
    }
}

TEST(MainWindowMenus, ShowTheShortcutBesideTheCommand) {
    MainWindow window;
    EXPECT_TRUE(window.menu_action_for(Action::slip_forward)->text().contains("Ctrl+Alt+Right"))
        << window.menu_action_for(Action::slip_forward)->text().toStdString();
    EXPECT_TRUE(window.menu_action_for(Action::nudge_forward)->text().contains("Alt+Right"));
    EXPECT_TRUE(window.menu_action_for(Action::play_stop)->text().contains("Space"));
}

TEST(MainWindowMenus, AMenuEntryPerformsTheSameEditAsItsKey) {
    MainWindow* by_menu = make_loaded_window();
    by_menu->menu_action_for(Action::slip_forward)->trigger();

    MainWindow* by_key = make_loaded_window();
    QTest::keyClick(by_key->timeline_panel(), Qt::Key_Right,
                    Qt::ControlModifier | Qt::AltModifier);

    EXPECT_EQ(rf::timeline::serialise(by_menu->document()),
              rf::timeline::serialise(by_key->document()));
    delete by_menu;
    delete by_key;
}

TEST(ToolPaletteTest, ARemappedKeyRelabelsItsButton) {
    const auto map = CommandMap::parse(
        "reelforge-keymap/1\n"
        "unbind B\n"
        "bind R select_tool_ripple\n");
    ASSERT_TRUE(map.has_value()) << map.error().to_string();
    const ToolPalette palette{map.value()};

    EXPECT_TRUE(palette.label_for(Action::select_tool_ripple).contains("R"));
    EXPECT_FALSE(palette.label_for(Action::select_tool_ripple).contains("B"));
}

// --- clicking and pressing are the same thing --------------------------------

TEST(ToolPaletteTest, ARippleTrimByMouseMatchesTheSameTrimByKey) {
    MainWindow* by_mouse = make_loaded_window();
    by_mouse->tool_palette()->button_for(Action::select_tool_ripple)->click();
    by_mouse->menu_action_for(Action::select_out_edge)->trigger();
    by_mouse->menu_action_for(Action::trim_forward)->trigger();

    MainWindow* by_key = make_loaded_window();
    QTest::keyClick(by_key->timeline_panel(), Qt::Key_B);
    QTest::keyClick(by_key->timeline_panel(), Qt::Key_BracketRight);
    QTest::keyClick(by_key->timeline_panel(), Qt::Key_Right, Qt::ControlModifier);

    EXPECT_EQ(rf::timeline::serialise(by_mouse->document()),
              rf::timeline::serialise(by_key->document()))
        << "a control and its shortcut must be the same edit, not two that agree today";

    delete by_mouse;
    delete by_key;
}

TEST(ToolPaletteTest, ClickingAButtonGivesFocusBackToTheTimeline) {
    // Otherwise the next keystroke goes to the button. Someone using the palette
    // to learn a key and then pressing it would find the key did nothing, which
    // is exactly the confusion this panel exists to remove.
    MainWindow* window = make_loaded_window();
    window->show();
    window->tool_palette()->button_for(Action::select_tool_ripple)->click();
    EXPECT_EQ(window->focusWidget(), window->timeline_panel());
    delete window;
}

// --- the palette shows what the keyboard did ---------------------------------

TEST(ToolPaletteTest, PressingAToolKeyChecksItsButton) {
    MainWindow* window = make_loaded_window();
    QTest::keyClick(window->timeline_panel(), Qt::Key_Y);

    EXPECT_TRUE(window->tool_palette()->button_for(Action::select_tool_slip)->isChecked());
    EXPECT_FALSE(window->tool_palette()->button_for(Action::select_tool_ripple)->isChecked());
    delete window;
}

TEST(ToolPaletteTest, OnlyOneSlotIsEverChecked) {
    MainWindow* window = make_loaded_window();
    for (const auto key : {Qt::Key_B, Qt::Key_N, Qt::Key_Y, Qt::Key_U, Qt::Key_V}) {
        QTest::keyClick(window->timeline_panel(), key);
        int checked = 0;
        for (QToolButton* button : window->tool_palette()->findChildren<QToolButton*>()) {
            checked += button->isChecked() ? 1 : 0;
        }
        EXPECT_EQ(checked, 1) << "after key " << key;
    }
    delete window;
}

// --- the status bar says what the next key will do ----------------------------

TEST(MainWindowStateLabel, NamesTheToolAndTheSelectedClip) {
    MainWindow* window = make_loaded_window();
    QTest::keyClick(window->timeline_panel(), Qt::Key_B);

    const QString text = window->edit_state_text();
    EXPECT_TRUE(text.contains("Ripple Edit")) << text.toStdString();
    EXPECT_TRUE(text.contains("demo_2")) << text.toStdString();
    delete window;
}

TEST(MainWindowStateLabel, ShowsTheEdgeOnlyForToolsThatMoveOne) {
    MainWindow* window = make_loaded_window();

    QTest::keyClick(window->timeline_panel(), Qt::Key_B);
    EXPECT_TRUE(window->edit_state_text().contains("out point"))
        << window->edit_state_text().toStdString();

    QTest::keyClick(window->timeline_panel(), Qt::Key_BracketLeft);
    EXPECT_TRUE(window->edit_state_text().contains("in point"));

    // Slip uses the whole clip, so offering a choice of edge would suggest a
    // control that does nothing.
    QTest::keyClick(window->timeline_panel(), Qt::Key_Y);
    EXPECT_FALSE(window->edit_state_text().contains("point"))
        << window->edit_state_text().toStdString();
    delete window;
}

TEST(MainWindowStateLabel, SaysSoWhenNothingIsSelected) {
    MainWindow window;
    EXPECT_TRUE(window.edit_state_text().contains("nothing selected"))
        << window.edit_state_text().toStdString();
}

TEST(MainWindowStateLabel, UpdatesEvenWhenTheKeyChangedNoClip) {
    // The original complaint: pressing B changed nothing visible, so there was
    // no way to tell whether the key had registered.
    MainWindow* window = make_loaded_window();
    const QString before = window->edit_state_text();
    QTest::keyClick(window->timeline_panel(), Qt::Key_N);
    EXPECT_NE(window->edit_state_text(), before);
    delete window;
}

}  // namespace
