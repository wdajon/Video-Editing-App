#include "rf/app/main_window.hpp"

#include <QAction>
#include <QMenuBar>
#include <QStatusBar>
#include <gtest/gtest.h>

#include "rf/core/version.hpp"

namespace {

using rf::app::MainWindow;

TEST(MainWindow, ConstructsWithNoProjectOpen) {
    MainWindow window;
    EXPECT_EQ(window.windowTitle(), QStringLiteral("ReelForge"));
    EXPECT_TRUE(window.project_path().isEmpty());
    EXPECT_FALSE(window.is_modified());
}

TEST(MainWindow, TitleTracksTheOpenProject) {
    MainWindow window;
    window.set_project(QStringLiteral("C:\\edits\\promo.rfproj"), false);
    EXPECT_EQ(window.windowTitle(), QStringLiteral("ReelForge - C:\\edits\\promo.rfproj"));

    window.set_project(QStringLiteral("C:\\edits\\promo.rfproj"), true);
    EXPECT_EQ(window.windowTitle(), QStringLiteral("ReelForge - C:\\edits\\promo.rfproj *"));

    window.set_project(QString(), false);
    EXPECT_EQ(window.windowTitle(), QStringLiteral("ReelForge"));
}

TEST(MainWindow, HasAFileMenuWithAQuitAction) {
    MainWindow window;
    auto* file_menu = window.findChild<QMenu*>(QStringLiteral("rf_menu_file"));
    ASSERT_NE(file_menu, nullptr) << "File menu is missing";
    EXPECT_EQ(file_menu->title(), QStringLiteral("&File"));

    auto* quit = window.findChild<QAction*>(QStringLiteral("rf_action_quit"));
    ASSERT_NE(quit, nullptr) << "Quit action is missing";
    EXPECT_FALSE(quit->shortcut().isEmpty()) << "Quit must be reachable from the keyboard";
}

TEST(MainWindow, StatusBarReportsBuildIdentity) {
    // A screenshot of the window has to be enough to identify the build a bug
    // report came from.
    MainWindow window;
    const QString status = window.statusBar()->currentMessage();
    const rf::BuildInfo info = rf::build_info();

    EXPECT_TRUE(status.contains(QString::fromUtf8(info.version.data(),
                                                  static_cast<qsizetype>(info.version.size()))))
        << status.toStdString();
    EXPECT_TRUE(status.contains(QString::fromUtf8(
        info.git_revision.data(), static_cast<qsizetype>(info.git_revision.size()))))
        << status.toStdString();
}

}  // namespace
