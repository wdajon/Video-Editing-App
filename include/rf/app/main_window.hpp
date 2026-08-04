#ifndef RF_APP_MAIN_WINDOW_HPP
#define RF_APP_MAIN_WINDOW_HPP

#include <QMainWindow>
#include <QString>

namespace rf::app {

/// The application shell.
///
/// M0 scope: a real window with a real menu bar and status bar, and nothing
/// that pretends to be a feature. Panels, docking, and workspaces arrive in M4;
/// there are deliberately no empty dock widgets standing in for them, because a
/// panel that docks but shows nothing is indistinguishable from a broken panel.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    /// Sets the open project and repaints the title bar accordingly. An empty
    /// path means "no project open".
    void set_project(const QString& project_path, bool modified);

    [[nodiscard]] QString project_path() const { return project_path_; }
    [[nodiscard]] bool is_modified() const { return modified_; }

private:
    void refresh_title();

    QString project_path_;
    bool modified_ = false;
};

}  // namespace rf::app

#endif  // RF_APP_MAIN_WINDOW_HPP
