#include "rf/app/main_window.hpp"

#include <QApplication>
#include <QDockWidget>
#include <QMenuBar>
#include <QStatusBar>

#include <string>
#include <utility>

#include "rf/app/timeline_panel.hpp"
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
    connect(timeline_panel_, &TimelinePanel::status_message, this, [this](const QString& message) {
        if (message.isEmpty()) {
            statusBar()->clearMessage();
        } else {
            statusBar()->showMessage(message);
        }
    });

    // The keyboard workflow starts here: without focus the panel never sees a
    // key press, and every trim in M4's gate is a key press.
    timeline_panel_->setFocus();

    refresh_title();
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
