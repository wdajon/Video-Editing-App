#include "rf/app/timeline_panel.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>

#include <algorithm>
#include <optional>
#include <vector>

#include "rf/app/key_translation.hpp"

namespace rf::app {
namespace {

constexpr int kTrackHeight = 56;
constexpr int kTrackGap = 4;
constexpr int kHeaderWidth = 96;
/// The strip along the top where dragging moves the playhead, as every editor
/// has. Clicking a clip below it selects; clicking up here scrubs.
constexpr int kRulerHeight = 22;

/// Ticks shown across the full width. Fixed for now: zoom is not implemented
/// (D24), and a zoom control nothing can drive would be decoration.
constexpr timeline::Ticks kVisibleTicks = 90000 * 20;

[[nodiscard]] int tick_to_x(timeline::Ticks tick, int width) {
    const int span = std::max(width - kHeaderWidth, 1);
    const double fraction = static_cast<double>(tick) / static_cast<double>(kVisibleTicks);
    return kHeaderWidth + static_cast<int>(fraction * span);
}

[[nodiscard]] int track_top(std::size_t index) {
    return kRulerHeight + kTrackGap +
           static_cast<int>(index) * (kTrackHeight + kTrackGap);
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

    const std::optional<edit::Action> action = map_.lookup(chord.value());
    if (!action) {
        // Most keys are bound to nothing. Letting them travel up keeps a menu
        // shortcut on the same key working.
        QWidget::keyPressEvent(event);
        return;
    }

    perform(*action);
    event->accept();
}

void TimelinePanel::perform(edit::Action action) {
    edit::Editor editor{document_, stack_, state_};
    const Result<void> performed = editor.perform(action);

    if (!performed) {
        last_message_ = QString::fromStdString(performed.error().message());
    } else {
        last_message_.clear();
    }
    Q_EMIT status_message(last_message_);

    // The arrow keys step the playhead through the edit state, so the clock has
    // to be told where the user put it -- otherwise the next tick would drag it
    // back to wherever playback thinks it is.
    if (state_.playhead_frame != playhead_frame_) {
        set_playhead_frame(state_.playhead_frame);
        Q_EMIT playhead_moved(state_.playhead_frame);
    }

    if (performed) {
        Q_EMIT document_changed();
        // The window owns the clock, so a shuttle action is reported rather
        // than applied here. Emitted unconditionally on success: working out
        // whether an action was a shuttle action would duplicate the map's job.
        Q_EMIT shuttle_changed();
    }
    // Emitted either way. A refused trim still leaves the tool and edge worth
    // showing, and the whole reason this signal exists is that a user could not
    // tell whether a key had done anything.
    Q_EMIT edit_state_changed();
    update();
}

timeline::Ticks TimelinePanel::tick_at(int x) const {
    const int span = std::max(width() - kHeaderWidth, 1);
    const double fraction = static_cast<double>(x - kHeaderWidth) / static_cast<double>(span);
    const auto tick = static_cast<timeline::Ticks>(fraction * static_cast<double>(kVisibleTicks));
    return std::max<timeline::Ticks>(0, tick);
}

int TimelinePanel::x_of_tick(timeline::Ticks tick) const {
    return tick_to_x(tick, width());
}

const timeline::Clip* TimelinePanel::clip_at(const QPoint& position,
                                             timeline::TrackId* track) const {
    const std::vector<timeline::Track>& tracks = document_.tracks();
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        const int top = track_top(i);
        if (position.y() < top || position.y() >= top + kTrackHeight) {
            continue;
        }
        const timeline::Ticks tick = tick_at(position.x());
        for (const timeline::Clip& clip : tracks[i].clips) {
            if (tick >= clip.start && tick < clip.start + clip.duration) {
                if (track != nullptr) {
                    *track = tracks[i].id;
                }
                return &clip;
            }
        }
        return nullptr;
    }
    return nullptr;
}

void TimelinePanel::scrub_to(int x) {
    const timeline::Ticks tick = tick_at(x);
    // Snapped to a frame, because a playhead between frames is not a position
    // anything else in ReelForge can represent.
    const std::int64_t frame = tick / document_.ticks_per_frame();
    state_.playhead_frame = frame;
    set_playhead_frame(frame);
    Q_EMIT playhead_moved(frame);
}

void TimelinePanel::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    setFocus();
    const QPoint position = event->position().toPoint();

    if (position.y() < kRulerHeight) {
        drag_ = Drag::playhead;
        scrub_to(position.x());
        event->accept();
        return;
    }

    timeline::TrackId track;
    const timeline::Clip* clip = clip_at(position, &track);
    if (clip == nullptr) {
        // Clicking empty space clears the selection, as it does everywhere else.
        state_.clip = timeline::ClipId{};
        drag_ = Drag::none;
    } else {
        state_.track = track;
        state_.clip = clip->id;
        drag_ = Drag::clip;
        drag_origin_tick_ = tick_at(position.x());
        drag_start_ = clip->start;
        drag_offset_ = 0;
    }
    Q_EMIT edit_state_changed();
    update();
    event->accept();
}

void TimelinePanel::mouseMoveEvent(QMouseEvent* event) {
    const QPoint position = event->position().toPoint();
    if (drag_ == Drag::playhead) {
        scrub_to(position.x());
        event->accept();
        return;
    }
    if (drag_ != Drag::clip) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    // Whole frames only, and nothing is committed yet: the clip is drawn at the
    // offset and the edit happens once, on release, so a drag is one undo entry
    // rather than one per pixel.
    const timeline::Ticks per_frame = document_.ticks_per_frame();
    const timeline::Ticks raw = tick_at(position.x()) - drag_origin_tick_;
    drag_offset_ = (raw / per_frame) * per_frame;
    update();
    event->accept();
}

void TimelinePanel::mouseReleaseEvent(QMouseEvent* event) {
    if (drag_ == Drag::none) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    const Drag finished = drag_;
    drag_ = Drag::none;

    if (finished != Drag::clip || drag_offset_ == 0) {
        // A drag that ended where it started is a click. No command, no undo
        // entry, and the project is not marked modified.
        drag_offset_ = 0;
        update();
        event->accept();
        return;
    }

    const timeline::Ticks target = std::max<timeline::Ticks>(0, drag_start_ + drag_offset_);
    const timeline::Ticks delta = target - drag_start_;
    drag_offset_ = 0;

    // Through the same nudge command the keyboard uses, so a drag moves a linked
    // pair together and lands in the history looking like any other edit.
    const Result<void> moved = stack_.execute(
        document_, timeline::make_trim(state_.clip, timeline::TrimKind::nudge, delta));
    if (!moved) {
        // Refused rather than clamped: dropping a clip onto another leaves it
        // where it was. Clamping would put it somewhere nobody pointed at.
        last_message_ = QString::fromStdString(moved.error().message());
    } else {
        last_message_.clear();
        Q_EMIT document_changed();
    }
    Q_EMIT status_message(last_message_);
    Q_EMIT edit_state_changed();
    update();
    event->accept();
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

    // The ruler: the strip where dragging moves the playhead.
    const QRect ruler(0, 0, width(), kRulerHeight);
    painter.fillRect(ruler, palette().window());
    painter.setPen(palette().color(QPalette::WindowText));
    painter.drawText(ruler.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                     tr("Drag here to move the playhead"));

    // Painted straight from the document rather than from a cached copy, so the
    // panel cannot show a timeline the document does not have.
    std::size_t track_index = 0;
    int y = track_top(0);
    for (const timeline::Track& track : document_.tracks()) {
        y = track_top(track_index++);
        const QRect header(0, y, kHeaderWidth, kTrackHeight);
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(header, Qt::AlignVCenter | Qt::AlignLeft,
                         QString::fromStdString(track.name));

        for (const timeline::Clip& clip : track.clips) {
            const bool selected = clip.id == state_.clip;
            // A clip being dragged is drawn where the pointer has it, not where
            // the document still says it is -- the document does not change
            // until the button comes up.
            const timeline::Ticks drawn_start =
                clip.start + (selected && drag_ == Drag::clip ? drag_offset_ : 0);
            const int left = tick_to_x(std::max<timeline::Ticks>(0, drawn_start), width());
            const int right = tick_to_x(std::max<timeline::Ticks>(0, drawn_start) + clip.duration,
                                        width());
            const QRect body(left, y, std::max(right - left, 2), kTrackHeight);
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
    }

    // The playhead, drawn last so it sits over the clips. JKL moves it, the
    // arrow keys step it, and dragging the ruler scrubs it.
    const timeline::Ticks playhead_tick = playhead_frame_ * document_.ticks_per_frame();
    const int playhead_x = tick_to_x(playhead_tick, width());
    if (playhead_x >= kHeaderWidth && playhead_x < width()) {
        painter.fillRect(QRect(playhead_x, 0, 2, height()),
                         palette().color(QPalette::BrightText));
    }
}

}  // namespace rf::app
