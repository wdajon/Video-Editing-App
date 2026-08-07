// The palette exists because a user could not tell whether a key had worked.
// So what these assert is mostly *visibility*: that a button says which key it
// is, that the palette shows which tool is live, and that clicking and pressing
// reach the same code.

#include "rf/app/tool_palette.hpp"

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QTest>

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

TEST(ToolPaletteTest, EveryButtonShowsItsOwnShortcut) {
    // Read from the live command map, never written beside the button, so a
    // remapped key relabels itself and a button cannot advertise a key that
    // does nothing.
    const CommandMap map = CommandMap::defaults();
    const ToolPalette palette{map};

    EXPECT_TRUE(palette.label_for(Action::select_tool_ripple).contains("B"));
    EXPECT_TRUE(palette.label_for(Action::select_tool_roll).contains("N"));
    EXPECT_TRUE(palette.label_for(Action::select_tool_slip).contains("Y"));
    EXPECT_TRUE(palette.label_for(Action::select_tool_slide).contains("U"));
    EXPECT_TRUE(palette.label_for(Action::trim_forward).contains("Ctrl+Right"))
        << palette.label_for(Action::trim_forward).toStdString();
    EXPECT_TRUE(palette.label_for(Action::trim_forward_many).contains("Ctrl+Shift+Right"));
    EXPECT_TRUE(palette.label_for(Action::shuttle_forward).contains("L"));
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

TEST(ToolPaletteTest, EveryActionWithAButtonHasOne) {
    const CommandMap map = CommandMap::defaults();
    const ToolPalette palette{map};
    for (const Action action :
         {Action::select_tool_selection, Action::select_tool_ripple, Action::select_tool_roll,
          Action::select_tool_slip, Action::select_tool_slide, Action::select_in_edge,
          Action::select_out_edge, Action::trim_backward, Action::trim_forward,
          Action::trim_backward_many, Action::trim_forward_many, Action::select_previous_clip,
          Action::select_next_clip, Action::select_previous_track, Action::select_next_track,
          Action::shuttle_backward, Action::shuttle_stop, Action::shuttle_forward, Action::undo,
          Action::redo}) {
        EXPECT_NE(palette.button_for(action), nullptr)
            << rf::edit::to_string(action) << " has no button";
    }
}

// --- clicking and pressing are the same thing --------------------------------

TEST(ToolPaletteTest, ClickingARippleButtonTrimsExactlyAsTheKeyDoes) {
    MainWindow* by_click = make_loaded_window();
    by_click->tool_palette()->button_for(Action::select_tool_ripple)->click();
    by_click->tool_palette()->button_for(Action::select_out_edge)->click();
    by_click->tool_palette()->button_for(Action::trim_forward)->click();

    MainWindow* by_key = make_loaded_window();
    QTest::keyClick(by_key->timeline_panel(), Qt::Key_B);
    QTest::keyClick(by_key->timeline_panel(), Qt::Key_BracketRight);
    QTest::keyClick(by_key->timeline_panel(), Qt::Key_Right, Qt::ControlModifier);

    EXPECT_EQ(rf::timeline::serialise(by_click->document()),
              rf::timeline::serialise(by_key->document()))
        << "a button and its shortcut must be the same edit, not two edits that agree today";

    delete by_click;
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

TEST(ToolPaletteTest, PressingAnEdgeKeyChecksItsButton) {
    MainWindow* window = make_loaded_window();
    QTest::keyClick(window->timeline_panel(), Qt::Key_BracketLeft);
    EXPECT_TRUE(window->tool_palette()->button_for(Action::select_in_edge)->isChecked());
    EXPECT_FALSE(window->tool_palette()->button_for(Action::select_out_edge)->isChecked());
    delete window;
}

TEST(ToolPaletteTest, OnlyOneToolIsEverChecked) {
    MainWindow* window = make_loaded_window();
    for (const auto key : {Qt::Key_B, Qt::Key_N, Qt::Key_Y, Qt::Key_U, Qt::Key_V}) {
        QTest::keyClick(window->timeline_panel(), key);
        int checked = 0;
        for (const Action action :
             {Action::select_tool_selection, Action::select_tool_ripple, Action::select_tool_roll,
              Action::select_tool_slip, Action::select_tool_slide}) {
            if (window->tool_palette()->button_for(action)->isChecked()) {
                ++checked;
            }
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
