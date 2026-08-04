#include "rf/app/main_window.hpp"

#include <QApplication>
#include <QMenuBar>
#include <QStatusBar>

#include <string>

#include "rf/app/window_title.hpp"
#include "rf/core/version.hpp"

namespace rf::app {

namespace {

constexpr auto kApplicationName = "ReelForge";

QString to_qstring(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
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

    refresh_title();
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
