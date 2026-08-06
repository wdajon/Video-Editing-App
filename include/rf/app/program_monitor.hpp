// The Program monitor: composited frames on a screen.
//
// A QWindow rather than a QWidget, because it owns a Vulkan surface. Qt supplies
// only the surface (ADR 008); ReelForge keeps the instance, device, compositor
// and pacing.

#ifndef RF_APP_PROGRAM_MONITOR_HPP
#define RF_APP_PROGRAM_MONITOR_HPP

#include <QWindow>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "rf/core/result.hpp"

namespace rf::app {

/// Plays a composited timeline into a window.
///
/// Presentation is the one part of ReelForge with no automated oracle: a test
/// can prove the composited texture is correct pixel for pixel, but only a
/// person can confirm the frames reached the monitor. The frame statistics this
/// reports on close are the mechanical half of that.
class ProgramMonitor : public QWindow {
    Q_OBJECT

public:
    ProgramMonitor();
    ~ProgramMonitor() override;

    /// Brings up Vulkan and, optionally, opens a video for layer 0.
    ///
    /// Fails when the machine cannot present at all, which is a reportable
    /// condition rather than a crash: a user with a broken driver should be
    /// told, not shown a dead window.
    [[nodiscard]] Result<void> initialise(const std::string& video_path);

    /// Frames presented, and frames the playhead passed that never reached the
    /// screen. Read after playback to check the run mechanically.
    [[nodiscard]] std::int64_t presented_frames() const noexcept;
    [[nodiscard]] std::int64_t dropped_frames() const noexcept;

    /// Present-to-present p99, in milliseconds.
    [[nodiscard]] double interval_p99_ms() const noexcept;

protected:
    void exposeEvent(QExposeEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool event(QEvent* event) override;

private:
    void render_one_frame();

    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rf::app

#endif  // RF_APP_PROGRAM_MONITOR_HPP
