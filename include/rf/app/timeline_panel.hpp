// The Timeline panel: tracks, clips, and the keyboard.
//
// The one panel this milestone ships, because it is the only one with something
// to draw (ADR 013). It owns nothing -- the document, the undo stack, the edit
// state and the command map all belong to the window -- which is what lets a
// test build a panel around its own document and drive it with real key events.

#ifndef RF_APP_TIMELINE_PANEL_HPP
#define RF_APP_TIMELINE_PANEL_HPP

#include <QString>
#include <QWidget>

#include <cstdint>

#include "rf/edit/command_map.hpp"
#include "rf/edit/editor.hpp"
#include "rf/timeline/command.hpp"
#include "rf/timeline/document.hpp"

namespace rf::app {

class TimelinePanel : public QWidget {
    Q_OBJECT

public:
    TimelinePanel(timeline::Document& document, timeline::CommandStack& stack,
                  edit::EditState& state, const edit::CommandMap& map,
                  QWidget* parent = nullptr);

    [[nodiscard]] const edit::EditState& edit_state() const noexcept { return state_; }

    /// Where the playhead is drawn, in frames. The window drives this from the
    /// transport as the clock advances; the panel does not own a timer, so a
    /// test can place the playhead exactly instead of waiting for one.
    void set_playhead_frame(std::int64_t frame);
    [[nodiscard]] std::int64_t playhead_frame() const noexcept { return playhead_frame_; }

    /// Performs `action` exactly as a key press would.
    ///
    /// The single path a key and a palette button both take. Two entry points
    /// that each interpreted an action would be two things to keep in step, and
    /// the first divergence would be a button that quietly did something its
    /// shortcut did not.
    void perform(edit::Action action);

    /// The message from the last refused action, or empty. A keyboard-only user
    /// pressing a trim key at the media limit has to be told why nothing moved,
    /// and a modal dialog on every refused keystroke would be unusable.
    [[nodiscard]] const QString& last_message() const noexcept { return last_message_; }

Q_SIGNALS:
    /// Emitted after any key that changed something, so the window can retitle
    /// itself and any other panel can refresh.
    void document_changed();

    /// Emitted when a key moved the shuttle, so the window can apply the new
    /// rate to the transport. The panel does not own the clock.
    void shuttle_changed();

    /// Emitted when the playhead was moved by the user -- an arrow key or a drag
    /// in the ruler -- so the window can re-anchor the playback clock there.
    void playhead_moved(std::int64_t frame);

    /// Emitted after every action, so the palette can show which tool and edge
    /// are live and the window can describe the state. This is what makes a key
    /// press visibly do something even when it changes no clip.
    void edit_state_changed();

    /// Emitted for a refused action, with the reason. Empty when an action
    /// succeeds, so a status bar clears itself rather than showing a stale
    /// complaint about a key pressed several edits ago.
    void status_message(const QString& message);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    /// What a press started. A drag is one gesture and must produce one undo
    /// entry, so the commit happens on release rather than on every move.
    enum class Drag : std::uint8_t {
        none,
        clip,      ///< Moving the selected clip along its track.
        playhead,  ///< Scrubbing in the ruler.
    };

    [[nodiscard]] timeline::Ticks tick_at(int x) const;
    [[nodiscard]] int x_of_tick(timeline::Ticks tick) const;
    /// The clip under `position`, or null.
    [[nodiscard]] const timeline::Clip* clip_at(const QPoint& position,
                                                timeline::TrackId* track) const;
    void scrub_to(int x);

private:
    timeline::Document& document_;
    timeline::CommandStack& stack_;
    edit::EditState& state_;
    const edit::CommandMap& map_;
    QString last_message_;
    std::int64_t playhead_frame_ = 0;

    Drag drag_ = Drag::none;
    timeline::Ticks drag_origin_tick_ = 0;   ///< Where the press landed.
    timeline::Ticks drag_start_ = 0;         ///< The clip's start when the drag began.
    timeline::Ticks drag_offset_ = 0;        ///< Live offset, drawn but not committed.
};

}  // namespace rf::app

#endif  // RF_APP_TIMELINE_PANEL_HPP
