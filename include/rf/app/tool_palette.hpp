// Buttons for everything the keyboard can do, each showing its own shortcut.
//
// Premiere puts its tools in a palette you can click, and hovering one tells you
// its key. That is how a keyboard-first application stays learnable: the mouse
// is the way you find out what the keys are.
//
// Every button performs an `Action` -- the same enumeration a key press resolves
// to -- so a button and its shortcut cannot drift apart. The label is read from
// the live `CommandMap` rather than written next to the button, so a remapped
// key relabels itself and a button can never claim a shortcut that no longer
// works. See docs/adr/015-tool-palette.md.

#ifndef RF_APP_TOOL_PALETTE_HPP
#define RF_APP_TOOL_PALETTE_HPP

#include <QHash>
#include <QString>
#include <QWidget>

#include <vector>

#include "rf/edit/action.hpp"
#include "rf/edit/command_map.hpp"

class QAbstractButton;
class QVBoxLayout;

namespace rf::app {

class ToolPalette : public QWidget {
    Q_OBJECT

public:
    ToolPalette(const edit::CommandMap& map, QWidget* parent = nullptr);

    /// Marks `tool` as the active one, so the palette shows what a trim key
    /// would do. Called by the window whenever the edit state changes, from a
    /// key press as readily as from a click.
    void set_active_tool(edit::Tool tool);

    /// Marks which edge a ripple or roll would move.
    void set_active_edge(edit::Edge edge);

    /// The button bound to `action`, for tests and for anything that needs to
    /// drive the palette without a mouse. Null if the action has no button.
    [[nodiscard]] QAbstractButton* button_for(edit::Action action) const;

    /// Text shown on the button for `action`, including its shortcut.
    [[nodiscard]] QString label_for(edit::Action action) const;

Q_SIGNALS:
    void action_triggered(edit::Action action);

private:
    void add_section(const QString& title);
    void add_button(edit::Action action, const QString& text, const QString& description,
                    bool checkable);

    const edit::CommandMap& map_;
    QHash<int, QAbstractButton*> buttons_;
    QVBoxLayout* layout_ = nullptr;
};

}  // namespace rf::app

#endif  // RF_APP_TOOL_PALETTE_HPP
