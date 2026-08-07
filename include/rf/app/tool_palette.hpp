// The Tools strip: a narrow column of icons, as Premiere has.
//
// Only tools live here. An earlier version listed every command as a full-width
// button, which swallowed the window and buried the Timeline behind it -- the
// panel is a tool chooser, not a menu. Everything else moved to the menu bar,
// which is where Premiere keeps it and where a shortcut can be read off.
//
// Tools that share a slot -- ripple and rolling, slip and slide -- sit behind
// one button. Clicking it uses the tool showing; clicking and holding opens a
// flyout to change which one that is, exactly as Premiere does.
//
// See docs/adr/017-tools-strip.md.

#ifndef RF_APP_TOOL_PALETTE_HPP
#define RF_APP_TOOL_PALETTE_HPP

#include <QHash>
#include <QString>
#include <QWidget>

#include <vector>

#include "rf/edit/action.hpp"
#include "rf/edit/command_map.hpp"

class QAction;
class QToolButton;
class QVBoxLayout;

namespace rf::app {

class ToolPalette : public QWidget {
    Q_OBJECT

public:
    ToolPalette(const edit::CommandMap& map, QWidget* parent = nullptr);

    /// Marks `tool` as active, and makes its slot show it. Called whenever the
    /// edit state changes, from a key as readily as from a click.
    void set_active_tool(edit::Tool tool);

    /// The slot button a tool lives in. Several tools share one.
    [[nodiscard]] QToolButton* button_for(edit::Action action) const;

    /// The flyout entry for a tool, which is how a shared slot is changed.
    [[nodiscard]] QAction* menu_action_for(edit::Action action) const;

    /// Text of the flyout entry, including its shortcut.
    [[nodiscard]] QString label_for(edit::Action action) const;

Q_SIGNALS:
    void action_triggered(edit::Action action);

private:
    /// One slot: the tools that share it, in flyout order.
    void add_slot(const std::vector<edit::Action>& tools);
    void show_in_slot(QToolButton* button, edit::Action tool);

    const edit::CommandMap& map_;
    QHash<int, QToolButton*> slot_of_tool_;
    QHash<int, QAction*> entry_of_tool_;
    QVBoxLayout* layout_ = nullptr;
};

}  // namespace rf::app

#endif  // RF_APP_TOOL_PALETTE_HPP
