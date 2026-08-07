#ifndef RF_APP_MAIN_WINDOW_HPP
#define RF_APP_MAIN_WINDOW_HPP

#include <QByteArray>
#include <QHash>
#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <memory>

#include "rf/app/transport.hpp"
#include "rf/core/result.hpp"
#include "rf/edit/command_map.hpp"
#include "rf/edit/editor.hpp"
#include "rf/playback/clock.hpp"
#include "rf/timeline/command.hpp"
#include "rf/timeline/document.hpp"

class QAction;
class QLabel;
class QMenu;

namespace rf::app {

class TimelinePanel;
class ToolPalette;

/// The application shell.
///
/// M0's rule still holds and still shapes this: there are no empty dock widgets
/// standing in for panels that do not exist, because a panel that docks but
/// shows nothing is indistinguishable from a broken one. M4 adds the Timeline,
/// which is the only panel with something to draw -- see
/// docs/adr/013-panels-and-workspaces.md for why Project, Source and Effect
/// Controls are absent rather than empty.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    /// Sets the open project and repaints the title bar accordingly. An empty
    /// path means "no project open".
    void set_project(const QString& project_path, bool modified);

    [[nodiscard]] QString project_path() const { return project_path_; }
    [[nodiscard]] bool is_modified() const { return modified_; }

    [[nodiscard]] timeline::Document& document() noexcept { return document_; }
    [[nodiscard]] edit::EditState& edit_state() noexcept { return edit_state_; }
    [[nodiscard]] TimelinePanel* timeline_panel() const noexcept { return timeline_panel_; }
    [[nodiscard]] ToolPalette* tool_palette() const noexcept { return tool_palette_; }

    /// What the status bar says the next trim key will do. Exposed because "the
    /// user could not tell whether a key had worked" is the defect this answers,
    /// so it is worth asserting rather than eyeballing.
    [[nodiscard]] QString edit_state_text() const;

    /// The menu entry for `action`, or null. Every command that is not a tool
    /// lives in a menu rather than on the Tools strip -- the strip is a tool
    /// chooser, and a wall of buttons there swallowed the window.
    [[nodiscard]] QAction* menu_action_for(edit::Action action) const;
    [[nodiscard]] Transport& transport() noexcept { return *transport_; }

    /// Reads the transport and moves the drawn playhead. Called on a timer while
    /// the shuttle runs, and directly by tests, which is why it is not private:
    /// a playhead that only a timer can move cannot be asserted on without
    /// waiting for one.
    void refresh_playhead(playback::Nanoseconds now);

    // --- workspaces ----------------------------------------------------------
    // A workspace is Qt's own dock layout under a name. ReelForge does not
    // invent a layout format; `QMainWindow::saveState` already produces one that
    // survives a Qt upgrade or refuses to load, which is more than a hand-rolled
    // one would. See docs/adr/013-panels-and-workspaces.md.

    /// Stores the current dock layout under `name`, replacing any layout
    /// already saved there.
    void save_workspace(const QString& name);

    /// Restores a saved layout.
    ///
    /// Fails when the name is unknown, and when Qt refuses the stored bytes --
    /// which it does rather than corrupting the window if it does not recognise
    /// the format. Propagated rather than ignored, because a workspace that
    /// silently did nothing looks like a broken layout.
    [[nodiscard]] Result<void> restore_workspace(const QString& name);

    [[nodiscard]] QStringList workspace_names() const;

private:
    void refresh_title();
    void refresh_state_label();
    /// Adds a menu entry that performs `action`, labelled with its shortcut read
    /// from the command map.
    void add_command(QMenu* menu, edit::Action action, const QString& text);

    timeline::Document document_;
    timeline::CommandStack stack_;
    edit::EditState edit_state_;
    edit::CommandMap command_map_;
    TimelinePanel* timeline_panel_ = nullptr;
    ToolPalette* tool_palette_ = nullptr;
    QLabel* state_label_ = nullptr;
    QHash<int, QAction*> command_actions_;
    /// By pointer because Transport has no default constructor -- it is built
    /// through a Result, since a frame rate can be refused.
    std::unique_ptr<Transport> transport_;
    playback::SteadyClock wall_clock_;
    QTimer* playhead_timer_ = nullptr;
    QHash<QString, QByteArray> workspaces_;

    QString project_path_;
    bool modified_ = false;
};

}  // namespace rf::app

#endif  // RF_APP_MAIN_WINDOW_HPP
