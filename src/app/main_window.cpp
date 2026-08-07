#include "rf/app/main_window.hpp"

#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QMenuBar>
#include <QStatusBar>
#include <QTimer>

#include <memory>
#include <string>
#include <utility>

#include "rf/app/timeline_panel.hpp"
#include "rf/app/tool_palette.hpp"
#include "rf/app/window_title.hpp"
#include "rf/core/assert.hpp"
#include "rf/core/version.hpp"
#include "rf/media/rational.hpp"

namespace rf::app {

namespace {

constexpr auto kApplicationName = "ReelForge";

QString to_qstring(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

/// 1/90000 ticks at 30 fps -- one frame is exactly 3000 ticks. A real project
/// will carry its own rate; this is what an empty window starts with.
timeline::Document default_document() {
    auto created = timeline::Document::create(media::Rational{1, 90000}, media::Rational{30, 1});
    RF_CHECK_MSG(created.has_value(), "the default document must be constructible");
    return std::move(created).value();
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      document_(default_document()),
      command_map_(edit::CommandMap::defaults()) {
    setObjectName("rf_main_window");
    resize(1600, 900);

    auto transport = Transport::create(document_.frame_rate());
    RF_CHECK_MSG(transport.has_value(), "the default document's frame rate must be playable");
    transport_ = std::make_unique<Transport>(std::move(transport).value());

    playhead_timer_ = new QTimer(this);
    playhead_timer_->setInterval(1000 / 60);
    connect(playhead_timer_, &QTimer::timeout, this,
            [this] { refresh_playhead(wall_clock_.now()); });

    QMenu* file_menu = menuBar()->addMenu(tr("&File"));
    file_menu->setObjectName("rf_menu_file");
    QAction* quit_action = file_menu->addAction(tr("E&xit"));
    quit_action->setObjectName("rf_action_quit");
    quit_action->setMenuRole(QAction::QuitRole);
    quit_action->setShortcut(QKeySequence::Quit);
    connect(quit_action, &QAction::triggered, this, &QWidget::close);

    // Build identity is on the status bar rather than behind a Help > About
    // dialog so a bug report can be produced from a screenshot.
    const BuildInfo info = build_info();
    statusBar()->showMessage(QStringLiteral("%1 %2 (%3, %4)")
                                 .arg(QString::fromLatin1(kApplicationName),
                                      to_qstring(info.version),
                                      to_qstring(info.git_revision),
                                      to_qstring(info.build_type)));

    // The Timeline is the only panel this milestone ships, because it is the
    // only one with something to draw. Empty docks standing in for Project,
    // Source and Effect Controls would look finished and show nothing --
    // docs/adr/013-panels-and-workspaces.md.
    auto* timeline_dock = new QDockWidget(tr("Timeline"), this);
    // Qt keys its saved layout state on objectName. A dock without a stable one
    // silently loses its position on restore, which reads as a layout bug rather
    // than a naming one.
    timeline_dock->setObjectName("rf_dock_timeline");
    timeline_panel_ = new TimelinePanel(document_, stack_, edit_state_, command_map_, this);
    timeline_dock->setWidget(timeline_panel_);
    addDockWidget(Qt::BottomDockWidgetArea, timeline_dock);

    connect(timeline_panel_, &TimelinePanel::document_changed, this, [this] {
        modified_ = true;
        refresh_title();
    });
    // Premiere puts its tools in a palette you can click, and hovering one tells
    // you its key. That is how a keyboard-first application stays learnable --
    // the mouse is how you find out what the keys are.
    auto* tools_dock = new QDockWidget(tr("Tools"), this);
    tools_dock->setObjectName("rf_dock_tools");
    tool_palette_ = new ToolPalette(command_map_, this);
    tools_dock->setWidget(tool_palette_);
    addDockWidget(Qt::LeftDockWidgetArea, tools_dock);

    // A button performs the action; it does not reimplement it. Both routes end
    // in the same call, so a button and its shortcut cannot come to mean
    // different things.
    connect(tool_palette_, &ToolPalette::action_triggered, timeline_panel_,
            &TimelinePanel::perform);

    connect(timeline_panel_, &TimelinePanel::edit_state_changed, this, [this] {
        tool_palette_->set_active_tool(edit_state_.tool);
        tool_palette_->set_active_edge(edit_state_.edge);
        refresh_state_label();
        // Clicking a button moves focus to it, and the next keystroke would go
        // to the button rather than the timeline. Handing focus back is what
        // lets someone use the palette to learn a key and then just press it.
        timeline_panel_->setFocus();
    });

    connect(timeline_panel_, &TimelinePanel::shuttle_changed, this, [this] {
        const playback::Nanoseconds now = wall_clock_.now();
        if (Result<void> applied = transport_->apply(edit_state_.shuttle, now); !applied) {
            statusBar()->showMessage(QString::fromStdString(applied.error().message()));
            return;
        }
        // Repaint at the sequence rate while shuttling, and stop the timer when
        // the shuttle does: a timer running against a stopped clock would burn
        // a core to draw the same playhead (the idle CPU budget is < 2%).
        if (transport_->is_playing()) {
            playhead_timer_->start();
        } else {
            playhead_timer_->stop();
        }
        refresh_playhead(now);
    });

    connect(timeline_panel_, &TimelinePanel::status_message, this, [this](const QString& message) {
        if (message.isEmpty()) {
            statusBar()->clearMessage();
        } else {
            statusBar()->showMessage(message);
        }
    });

    // Permanently on the right of the status bar, beside the transient
    // messages: what the next trim key will do. Without it a user pressing B has
    // no evidence anything happened at all.
    state_label_ = new QLabel(this);
    state_label_->setObjectName("rf_label_edit_state");
    statusBar()->addPermanentWidget(state_label_);
    refresh_state_label();

    // The keyboard workflow starts here: without focus the panel never sees a
    // key press, and every trim in M4's gate is a key press.
    timeline_panel_->setFocus();

    refresh_title();
}

void MainWindow::refresh_state_label() {
    const QString tool = QString::fromStdString(std::string(edit::to_string(edit_state_.tool)));

    QString clip = tr("nothing selected");
    if (const timeline::Clip* selected = document_.find_clip(edit_state_.clip);
        selected != nullptr) {
        clip = QString::fromStdString(selected->source);
    }

    // The edge only means something for the two tools that move one. Showing it
    // for slip would suggest a choice that has no effect.
    const bool edge_matters =
        edit_state_.tool == edit::Tool::ripple || edit_state_.tool == edit::Tool::roll;
    const QString edge =
        edge_matters
            ? tr(" · %1 point")
                  .arg(edit_state_.edge == edit::Edge::in ? tr("in") : tr("out"))
            : QString{};

    state_label_->setText(tr("%1 · %2%3").arg(tool, clip, edge));
}

void MainWindow::refresh_playhead(playback::Nanoseconds now) {
    const Result<std::int64_t> frame = transport_->frame_at(now);
    if (!frame) {
        // An absurd position rather than a normal outcome. Stopping is the only
        // safe response: leaving the timer running would repeat the failure at
        // the frame rate.
        playhead_timer_->stop();
        statusBar()->showMessage(QString::fromStdString(frame.error().message()));
        return;
    }
    timeline_panel_->set_playhead_frame(frame.value());
}

void MainWindow::save_workspace(const QString& name) {
    workspaces_.insert(name, saveState());
}

Result<void> MainWindow::restore_workspace(const QString& name) {
    const auto found = workspaces_.constFind(name);
    if (found == workspaces_.constEnd()) {
        return Error{Errc::not_found, "no workspace named '" + name.toStdString() + "'"};
    }
    if (!restoreState(found.value())) {
        // Qt refuses state it does not recognise rather than corrupting the
        // window. Reporting that beats a workspace that silently does nothing.
        return Error{Errc::version_mismatch,
                     "workspace '" + name.toStdString() + "' was not accepted by Qt"};
    }
    return ok();
}

QString MainWindow::edit_state_text() const {
    return state_label_ == nullptr ? QString{} : state_label_->text();
}

QStringList MainWindow::workspace_names() const {
    QStringList names = workspaces_.keys();
    names.sort();
    return names;
}

void MainWindow::set_project(const QString& project_path, bool modified) {
    project_path_ = project_path;
    modified_ = modified;
    refresh_title();
}

void MainWindow::refresh_title() {
    const std::string project = project_path_.toStdString();
    setWindowTitle(QString::fromStdString(
        compose_window_title(kApplicationName, project, modified_)));
}

}  // namespace rf::app
