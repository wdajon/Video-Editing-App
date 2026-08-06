#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>

#include <cstdio>
#include <string>

#include "rf/app/main_window.hpp"
#include "rf/app/program_monitor.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ReelForge"));
    QCoreApplication::setOrganizationName(QStringLiteral("ReelForge"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("ReelForge - non-linear editor for short-form vertical video"));
    parser.addHelpOption();

    // Playing a file straight into the Program monitor is how presentation gets
    // verified at all: it is the one part of the project with no automated
    // oracle, so it has to be runnable by a person in one command.
    const QCommandLineOption play(QStringLiteral("play"),
                                  QStringLiteral("Play <file> in the Program monitor and exit."),
                                  QStringLiteral("file"));
    parser.addOption(play);
    parser.process(app);

    if (parser.isSet(play)) {
        rf::app::ProgramMonitor monitor;
        const std::string path = parser.value(play).toStdString();

        if (auto started = monitor.initialise(path); !started) {
            std::fprintf(stderr, "%s\n", started.error().to_string().c_str());
            return 1;
        }
        monitor.show();

        const int status = QApplication::exec();

        // The mechanical half of verifying presentation. Whether the frames
        // looked right is still a human's call (ADR 008).
        std::printf("presented: %lld frames\n",
                    static_cast<long long>(monitor.presented_frames()));
        std::printf("dropped:   %lld\n", static_cast<long long>(monitor.dropped_frames()));
        std::printf("interval p99: %.2f ms\n", monitor.interval_p99_ms());
        return status;
    }

    rf::app::MainWindow window;
    window.show();
    return QApplication::exec();
}
