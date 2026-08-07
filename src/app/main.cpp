#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>

#include <cstdio>
#include <string>

#include "rf/app/demo_timeline.hpp"
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

    // The Timeline panel's painting has no automated oracle, and neither does
    // the feel of a keyboard trim. Both need a person, and a person needs
    // something on screen -- there is no project loading yet, so an empty window
    // shows nothing to press keys against.
    const QCommandLineOption demo(
        QStringLiteral("demo-timeline"),
        QStringLiteral("Start with a timeline to try the keyboard against."));
    parser.addOption(demo);

    // Renders the window to a file and exits. The Timeline and the Tools strip
    // have no automated oracle (D23), and "the panel swallowed the window" is
    // exactly the kind of defect a person spots instantly and no assertion
    // catches. This is the mechanical half of looking at it, and it works on the
    // offscreen platform, so it can be run without a display.
    const QCommandLineOption screenshot(
        QStringLiteral("screenshot"),
        QStringLiteral("Render the window to <file> as PNG and exit."),
        QStringLiteral("file"));
    parser.addOption(screenshot);
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

    if (parser.isSet(demo)) {
        if (auto built = rf::app::build_demo_timeline(window.document()); !built) {
            std::fprintf(stderr, "%s\n", built.error().to_string().c_str());
            return 1;
        }
        // Select something, or the first trim key has nothing to act on and the
        // demo's first impression is an error message.
        window.edit_state().track = window.document().tracks().front().id;
        window.edit_state().clip = window.document().tracks().front().clips.front().id;
    }

    window.show();

    if (parser.isSet(screenshot)) {
        // One event loop pass so the layout settles before the grab; otherwise
        // the image shows widgets at their pre-layout sizes.
        QApplication::processEvents();
        const QString path = parser.value(screenshot);
        if (!window.grab().save(path, "PNG")) {
            std::fprintf(stderr, "could not write %s\n", path.toStdString().c_str());
            return 1;
        }
        std::printf("wrote %s\n", path.toStdString().c_str());
        return 0;
    }

    return QApplication::exec();
}
