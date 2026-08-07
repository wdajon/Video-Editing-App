// The Program monitor: the picture at the playhead.
//
// A plain QWidget that paints the frame `rf_render` produces, not the Vulkan
// `QWindow` of ADR 008. That is a deliberate trade and it is not the end state:
//
//   * This works with no surface, so it runs on the offscreen platform, is
//     testable on a CI runner, and shows a picture on any machine with a device.
//   * It reads pixels back from the GPU for every frame, which M3 measured at
//     p50 49.9 ms for 1080x1920 with three layers (D13). Fine for scrubbing and
//     stepping; **it will not sustain playback at a real sequence size.**
//
// The Vulkan path already exists and already presents under FIFO. Routing it
// through here is the rest of D30. Getting a picture into the window first is
// worth more than getting the fast one there eventually.

#ifndef RF_APP_PROGRAM_PANEL_HPP
#define RF_APP_PROGRAM_PANEL_HPP

#include <QImage>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <memory>

#include "rf/timeline/document.hpp"

namespace rf::app {

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

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    QImage picture_;
    QString status_;
    std::int64_t frame_ = -1;  ///< -1 so the first show_frame(0) is not a no-op.
};

}  // namespace rf::app

#endif  // RF_APP_PROGRAM_PANEL_HPP
