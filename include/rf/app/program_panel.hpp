// The Program monitor: the picture at the playhead.
//
// Two routes to the screen, and the panel picks whichever this machine can do:
//
//   * **Presenting.** A `ProgramSurface` -- the Vulkan `QWindow` of ADR 008 --
//     blits the composited texture into a swapchain image, embedded here with
//     `QWidget::createWindowContainer`.
//   * **Readback.** With no surface, the panel reads pixels off the device and
//     paints them itself. That works on the offscreen platform, so it is the
//     one a CI runner exercises, and it costs about 36 ms a frame more at
//     1080x1920 (D13): fine for scrubbing and stepping, **not enough to sustain
//     playback at a real sequence size.**
//
// `path()` says which one is live, and the difference is loud enough that the
// Program dock puts it in its title. Note the third state: until a frame has
// rendered, neither route is live and the honest answer is `unknown` rather
// than a guess in either direction.

#ifndef RF_APP_PROGRAM_PANEL_HPP
#define RF_APP_PROGRAM_PANEL_HPP

#include <QImage>
#include <QString>
#include <QWidget>

class QVBoxLayout;

#include <cstdint>
#include <memory>

#include "rf/timeline/document.hpp"

namespace rf::app {

class ProgramSurface;

class ProgramPanel : public QWidget {
    Q_OBJECT

public:
    explicit ProgramPanel(timeline::Document& document, QWidget* parent = nullptr);
    ~ProgramPanel() override;

    /// Renders `frame` and repaints. Cheap to call with the frame already
    /// showing -- it returns without re-rendering, so a repaint storm during a
    /// drag costs one decode rather than one per mouse move.
    void show_frame(std::int64_t frame);

    /// Forgets which frame is showing, so the next `show_frame` re-renders.
    /// An edit changes what is under the playhead without moving it -- a slip is
    /// exactly that, and it is the operation hardest to believe in without
    /// seeing it.
    void invalidate() noexcept { frame_ = -1; }

    /// Why there is no picture, or empty when there is one. A monitor that goes
    /// black without saying why is indistinguishable from a broken one, so the
    /// reason is drawn in the panel and readable from a test.
    [[nodiscard]] const QString& status() const noexcept { return status_; }

    /// The frame currently shown.
    [[nodiscard]] std::int64_t frame() const noexcept { return frame_; }

    /// True once a device and a renderer exist. False on a machine with no
    /// Vulkan device, where the panel says so rather than showing black.
    [[nodiscard]] bool can_render() const noexcept;

    /// Builds the Vulkan surface and puts it in the layout, when this machine
    /// can present. Separate from the constructor because it needs the device,
    /// which is brought up lazily on the first frame with a clip under it.
    ///
    /// Does nothing where presentation is unavailable: the readback path keeps
    /// working, only slower. Cheap to call repeatedly -- it returns immediately
    /// once a surface exists, which is what lets every refresh call it rather
    /// than only the one that happens to run first.
    void attach_surface();

    /// Which route the last frame took to the screen.
    ///
    /// Three states rather than a bool, because "nothing has rendered yet" is a
    /// different answer from "rendered, and read back" and the two used to be
    /// indistinguishable. That mattered: the dock title is the only signal
    /// anyone has for D30, and a launched-but-untried panel reported the same
    /// thing as a presenting one.
    enum class Path {
        unknown,     ///< Nothing has rendered, so no route is live.
        presenting,  ///< Blitted into a swapchain image and presented.
        readback,    ///< Read off the device and painted by this widget.
    };
    Q_ENUM(Path)

    [[nodiscard]] Path path() const noexcept { return path_; }

    /// Why presentation is not in use, or empty when it is -- and empty, too,
    /// when it has not been tried yet.
    ///
    /// Recorded rather than shown: a picture appears either way and the dock
    /// title already names the route. It exists because "never attempted" and
    /// "attempted and refused" look identical from outside and are very
    /// different bugs -- the first is what left the swapchain unused in any
    /// session where nobody pressed a shuttle key.
    [[nodiscard]] const QString& presentation_refusal() const noexcept { return refusal_; }

    /// True when frames go to a swapchain rather than through a readback.
    [[nodiscard]] bool is_presenting() const noexcept { return path_ == Path::presenting; }

signals:
    /// Emitted when the route changes, which happens twice on a machine that
    /// can present: once when the first frame is read back, and again when the
    /// surface is exposed and the swapchain takes over. A caller that only read
    /// the value once would report the wrong one.
    void path_changed(Path path);

public:

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    /// Records the route and tells anyone listening when it changes.
    void set_path(Path taken);

    class Impl;
    std::unique_ptr<Impl> impl_;
    ProgramSurface* surface_ = nullptr;
    QWidget* container_ = nullptr;
    QVBoxLayout* layout_ = nullptr;
    QImage picture_;
    QString status_;
    QString refusal_;
    Path path_ = Path::unknown;
    std::int64_t frame_ = -1;  ///< -1 so the first show_frame(0) is not a no-op.
};

}  // namespace rf::app

#endif  // RF_APP_PROGRAM_PANEL_HPP
