#include "rf/app/timeline_panel.hpp"

#include <QKeyEvent>
#include <QPainter>
#include <QPaintEvent>

#include <algorithm>

#include "rf/app/key_translation.hpp"

namespace rf::app {
namespace {

constexpr int kTrackHeight = 56;
constexpr int kTrackGap = 4;
constexpr int kHeaderWidth = 96;

/// Ticks shown across the full width. Fixed for now: this milestone is about the
/// keyboard, and a zoom control that nothing can drive would be decoration.
constexpr timeline::Ticks kVisibleTicks = 90000 * 20;

[[nodiscard]] int tick_to_x(timeline::Ticks tick, int width) {
    const int span = std::max(width - kHeaderWidth, 1);
    const double fraction = static_cast<double>(tick) / static_cast<double>(kVisibleTicks);
    return kHeaderWidth + static_cast<int>(fraction * span);
}

}  // namespace

TimelinePanel::TimelinePanel(timeline::Document& document, timeline::CommandStack& stack,
                             edit::EditState& state, const edit::CommandMap& map, QWidget* parent)
    : QWidget(parent), document_(document), stack_(stack), state_(state), map_(map) {
    setObjectName("rf_panel_timeline");
    // Without this the panel never receives a key press: Qt gives keyboard focus
    // only to widgets that ask for it, and a timeline that cannot be focused is
    // a timeline the gate's workflow cannot reach.
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(480, 200);
}

void TimelinePanel::keyPressEvent(QKeyEvent* event) {
    const std::optional<edit::KeyChord> chord = to_key_chord(*event);
    if (!chord) {
        // A key ReelForge has no name for is not ours; let it travel up so a
        // menu shortcut or the window still sees it.
        QWidget::keyPressEvent(event);
        return;
    }

    edit::Editor editor{document_, stack_, state_};
    const Result<void> performed = editor.press(map_, chord.value());
    if (!performed) {
        // An unbound chord is not a failure worth reporting -- most keys are
        // unbound. Anything else is something the user asked for and did not
        // get, so it has to be said out loud.
        if (performed.error().code() == Errc::not_found &&
            !map_.lookup(chord.value()).has_value()) {
            QWidget::keyPressEvent(event);
            return;
        }
        last_message_ = QString::fromStdString(performed.error().message());
        Q_EMIT status_message(last_message_);
        event->accept();
        update();
        return;
    }

    last_message_.clear();
    Q_EMIT status_message(QString{});
    Q_EMIT document_changed();
    // The window owns the clock, so a shuttle key is reported rather than
    // applied here. Emitted unconditionally on success: working out whether a
    // key was a shuttle key would duplicate the command map's job.
    Q_EMIT shuttle_changed();
    event->accept();
    update();
}

void TimelinePanel::set_playhead_frame(std::int64_t frame) {
    if (frame == playhead_frame_) {
        return;
    }
    playhead_frame_ = frame;
    update();
}

void TimelinePanel::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.fillRect(event->rect(), palette().base());

    // Painted straight from the document rather than from a cached copy, so the
    // panel cannot show a timeline the document does not have.
    int y = kTrackGap;
    for (const timeline::Track& track : document_.tracks()) {
        const QRect header(0, y, kHeaderWidth, kTrackHeight);
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(header, Qt::AlignVCenter | Qt::AlignLeft,
                         QString::fromStdString(track.name));

        for (const timeline::Clip& clip : track.clips) {
            const int left = tick_to_x(clip.start, width());
            const int right = tick_to_x(clip.start + clip.duration, width());
            const QRect body(left, y, std::max(right - left, 2), kTrackHeight);

            const bool selected = clip.id == state_.clip;
            painter.fillRect(body, selected ? palette().highlight() : palette().button());
            painter.setPen(palette().color(QPalette::ButtonText));
            painter.drawRect(body.adjusted(0, 0, -1, -1));
            painter.drawText(body.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                             QString::fromStdString(clip.source));

            // Which edge is armed is the difference between two trims that look
            // identical until they are performed, so it is drawn.
            if (selected) {
                const int edge_x = state_.edge == edit::Edge::in ? body.left() : body.right() - 2;
                painter.fillRect(QRect(edge_x, y, 3, kTrackHeight),
                                 palette().color(QPalette::HighlightedText));
            }
        }
        y += kTrackHeight + kTrackGap;
    }

    // The playhead, drawn last so it sits over the clips. This is what JKL
    // moves: the shuttle sets a rate, the clock turns wall time into a frame,
    // and the frame lands here.
    const timeline::Ticks playhead_tick = playhead_frame_ * document_.ticks_per_frame();
    const int playhead_x = tick_to_x(playhead_tick, width());
    if (playhead_x >= kHeaderWidth && playhead_x < width()) {
        painter.fillRect(QRect(playhead_x, 0, 2, std::max(y, height())),
                         palette().color(QPalette::BrightText));
    }
}

}  // namespace rf::app
