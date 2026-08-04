#include <QApplication>

#include "rf/app/main_window.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ReelForge"));
    QCoreApplication::setOrganizationName(QStringLiteral("ReelForge"));

    rf::app::MainWindow window;
    window.show();

    return QApplication::exec();
}
