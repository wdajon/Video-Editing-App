#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QImage>

#include <cstdio>
#include <string>

#include "rf/app/demo_timeline.hpp"
#include "rf/app/main_window.hpp"
#include "rf/app/program_monitor.hpp"
#include "rf/gpu/device.hpp"
#include "rf/gpu/instance.hpp"
#include "rf/render/sequence_renderer.hpp"
#include "rf/timeline/document.hpp"

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

    // Renders one frame of the timeline through the real path -- model, decoder,
    // converter, compositor -- and writes it out. The first way to look at what
    // an edit actually produces, and the mechanical half of the picture the
    // Program monitor will show live.
    const QCommandLineOption render_frame(
        QStringLiteral("render-frame"),
        QStringLiteral("Render timeline frame <n> to the file given by --out and exit."),
        QStringLiteral("n"));
    parser.addOption(render_frame);
    const QCommandLineOption out(QStringLiteral("out"),
                                 QStringLiteral("Output file for --render-frame."),
                                 QStringLiteral("file"));
    parser.addOption(out);
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
        // Point the demo at the fixture the repository ships when a picture is
        // wanted, and at a name with no file behind it otherwise. Nothing in the
        // editor decodes, so the editor demo does not need real media -- but
        // --render-frame does, and a demo that could not render would be a poor
        // way to show that rendering works.
        const std::string source =
            parser.isSet(render_frame) ? rf::app::demo_media_relative_path() : std::string{};
        if (auto built = rf::app::build_demo_timeline(window.document(), source); !built) {
            std::fprintf(stderr, "%s\n", built.error().to_string().c_str());
            return 1;
        }
        // Select something, or the first trim key has nothing to act on and the
        // demo's first impression is an error message. The *second* clip, so it
        // has a neighbour on both sides -- roll and slide need one, and the
        // first clip starts at zero with nothing to its left, so half the trim
        // set would be correctly refused on a clip anyone would try first.
        const rf::timeline::Track& video = window.document().tracks().front();
        window.edit_state().track = video.id;
        window.edit_state().clip = video.clips[video.clips.size() > 1 ? 1 : 0].id;
    }

    if (parser.isSet(render_frame)) {
        if (!parser.isSet(out)) {
            std::fprintf(stderr, "--render-frame needs --out\n");
            return 1;
        }
        bool numeric = false;
        const qlonglong frame = parser.value(render_frame).toLongLong(&numeric);
        if (!numeric || frame < 0) {
            std::fprintf(stderr, "--render-frame needs a frame number\n");
            return 1;
        }

        auto instance = rf::gpu::Instance::create(rf::gpu::Instance::Options{});
        if (!instance) {
            std::fprintf(stderr, "%s\n", instance.error().to_string().c_str());
            return 1;
        }
        auto device = rf::gpu::Device::create_preferred(instance.value());
        if (!device) {
            std::fprintf(stderr, "%s\n", device.error().to_string().c_str());
            return 1;
        }
        // The fixture's size. A sequence size that a project carries rather than
        // a flag chooses is M5's work; hard-coding it here would be a spec
        // number in a source file, so it comes from the media for now.
        auto renderer = rf::render::SequenceRenderer::create(device.value(), 320, 240);
        if (!renderer) {
            std::fprintf(stderr, "%s\n", renderer.error().to_string().c_str());
            return 1;
        }

        auto image = renderer.value().render(window.document(), frame);
        if (!image) {
            std::fprintf(stderr, "%s\n", image.error().to_string().c_str());
            return 1;
        }
        const QImage picture(image.value().pixels.data(), image.value().width,
                             image.value().height, image.value().width * 4,
                             QImage::Format_RGBA8888);
        if (!picture.save(parser.value(out), "PNG")) {
            std::fprintf(stderr, "could not write %s\n", parser.value(out).toStdString().c_str());
            return 1;
        }
        std::printf("rendered frame %lld to %s\n", static_cast<long long>(frame),
                    parser.value(out).toStdString().c_str());
        return 0;
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
