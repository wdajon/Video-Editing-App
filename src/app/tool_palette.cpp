#include "rf/app/tool_palette.hpp"

#include <QButtonGroup>
#include <QFont>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

#include <string>

namespace rf::app {
namespace {

using edit::Action;

/// Turns "Ctrl+Right" into "Ctrl+Right" but "Right" into "Right" -- i.e. the
/// canonical chord spelling, unchanged. Kept as a function because the label is
/// the one place a user compares what they read against what they pressed, and
/// any prettifying done here has to be done in exactly one place.
[[nodiscard]] QString shortcut_text(const edit::CommandMap& map, Action action) {
    const std::vector<edit::KeyChord> chords = map.chords_for(action);
    if (chords.empty()) {
        return {};
    }
    return QString::fromStdString(edit::to_string(chords.front()));
}

}  // namespace

ToolPalette::ToolPalette(const edit::CommandMap& map, QWidget* parent)
    : QWidget(parent), map_(map) {
    setObjectName("rf_panel_tools");

    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(6, 6, 6, 6);
    layout_->setSpacing(2);

    add_section(tr("Tools"));
    add_button(Action::select_tool_selection, tr("Selection"),
               tr("Select and move clips. Trim keys do nothing with this tool."), true);
    add_button(Action::select_tool_ripple, tr("Ripple Edit"),
               tr("Move an edit point; everything after it follows."), true);
    add_button(Action::select_tool_roll, tr("Rolling Edit"),
               tr("Move the boundary between two clips. The sequence keeps its length."), true);
    add_button(Action::select_tool_slip, tr("Slip"),
               tr("Change which part of the source the clip shows. Nothing moves."), true);
    add_button(Action::select_tool_slide, tr("Slide"),
               tr("Move a clip; its neighbours absorb the difference."), true);

    add_section(tr("Edit point"));
    add_button(Action::select_in_edge, tr("In point"),
               tr("Ripple and roll act on the start of the clip."), true);
    add_button(Action::select_out_edge, tr("Out point"),
               tr("Ripple and roll act on the end of the clip."), true);

    add_section(tr("Trim (ripple and roll)"));
    add_button(Action::trim_backward_many, tr("Back 5 frames"), {}, false);
    add_button(Action::trim_backward, tr("Back 1 frame"), {}, false);
    add_button(Action::trim_forward, tr("Forward 1 frame"), {}, false);
    add_button(Action::trim_forward_many, tr("Forward 5 frames"), {}, false);

    // Adobe's Timeline commands act on the selection with no tool involved, so
    // they get their own section rather than sitting under the tools they no
    // longer need. One press, and something moves.
    add_section(tr("Move selected clip"));
    add_button(Action::nudge_backward, tr("Nudge left"), {}, false);
    add_button(Action::nudge_forward, tr("Nudge right"), {}, false);
    add_button(Action::slip_backward, tr("Slip left"),
               tr("Change what the clip shows. It does not move."), false);
    add_button(Action::slip_forward, tr("Slip right"),
               tr("Change what the clip shows. It does not move."), false);
    add_button(Action::slide_backward, tr("Slide left"),
               tr("Move the clip; its neighbours absorb it."), false);
    add_button(Action::slide_forward, tr("Slide right"),
               tr("Move the clip; its neighbours absorb it."), false);

    add_section(tr("Select"));
    add_button(Action::select_previous_clip, tr("Previous clip"), {}, false);
    add_button(Action::select_next_clip, tr("Next clip"), {}, false);
    add_button(Action::select_previous_track, tr("Track above"), {}, false);
    add_button(Action::select_next_track, tr("Track below"), {}, false);

    add_section(tr("Transport"));
    add_button(Action::step_backward, tr("Step back"), {}, false);
    add_button(Action::step_forward, tr("Step forward"), {}, false);
    add_button(Action::play_stop, tr("Play / Stop"), {}, false);
    add_button(Action::shuttle_backward, tr("Shuttle back"),
               tr("Press again to go faster."), false);
    add_button(Action::shuttle_stop, tr("Stop"), {}, false);
    add_button(Action::shuttle_forward, tr("Shuttle forward"),
               tr("Press again to go faster."), false);

    add_section(tr("History"));
    add_button(Action::undo, tr("Undo"), {}, false);
    add_button(Action::redo, tr("Redo"), {}, false);

    layout_->addStretch(1);

    set_active_tool(edit::Tool::selection);
    set_active_edge(edit::Edge::out);
}

void ToolPalette::add_section(const QString& title) {
    auto* label = new QLabel(title, this);
    QFont bold = label->font();
    bold.setBold(true);
    label->setFont(bold);
    // A little air above each heading, except the first, so the groups read as
    // groups rather than as one long column of buttons.
    if (layout_->count() > 0) {
        layout_->addSpacing(8);
    }
    layout_->addWidget(label);
}

void ToolPalette::add_button(Action action, const QString& text, const QString& description,
                             bool checkable) {
    auto* button = new QToolButton(this);
    button->setObjectName(QStringLiteral("rf_tool_%1").arg(
        QString::fromStdString(std::string(edit::to_string(action)))));
    button->setCheckable(checkable);
    // Auto-raise draws the button flat, and a flat checked button is nearly
    // indistinguishable from an unchecked one in the default style -- so
    // selecting a tool looked like nothing had happened even though it had.
    // That was the whole complaint. See ADR 016.
    button->setAutoRaise(false);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    const QString shortcut = shortcut_text(map_, action);
    // The shortcut goes on the face of the button, not only in a tooltip: the
    // complaint that produced this palette was not knowing whether a key had
    // done anything, and a hint you have to hover to find does not answer that.
    // Separated with spaces, not a tab -- QToolButton does not expand tabs, so
    // a tab here renders as a stray glyph or as nothing at all.
    button->setText(shortcut.isEmpty() ? text
                                       : QStringLiteral("%1   (%2)").arg(text, shortcut));
    button->setToolTip(description.isEmpty() ? text
                                             : QStringLiteral("%1\n%2").arg(text, description));

    // The button performs the action; it does not reimplement it. This is what
    // stops a button and its shortcut from ever meaning different things.
    connect(button, &QToolButton::clicked, this,
            [this, action] { Q_EMIT action_triggered(action); });

    buttons_.insert(static_cast<int>(action), button);
    layout_->addWidget(button);
}

QAbstractButton* ToolPalette::button_for(Action action) const {
    return buttons_.value(static_cast<int>(action), nullptr);
}

QString ToolPalette::label_for(Action action) const {
    QAbstractButton* button = button_for(action);
    return button == nullptr ? QString{} : button->text();
}

void ToolPalette::set_active_tool(edit::Tool tool) {
    const QHash<edit::Tool, Action> actions{
        {edit::Tool::selection, Action::select_tool_selection},
        {edit::Tool::ripple, Action::select_tool_ripple},
        {edit::Tool::roll, Action::select_tool_roll},
        {edit::Tool::slip, Action::select_tool_slip},
        {edit::Tool::slide, Action::select_tool_slide},
    };
    for (auto it = actions.constBegin(); it != actions.constEnd(); ++it) {
        if (QAbstractButton* button = button_for(it.value()); button != nullptr) {
            button->setChecked(it.key() == tool);
        }
    }
}

void ToolPalette::set_active_edge(edit::Edge edge) {
    if (QAbstractButton* in = button_for(Action::select_in_edge); in != nullptr) {
        in->setChecked(edge == edit::Edge::in);
    }
    if (QAbstractButton* out = button_for(Action::select_out_edge); out != nullptr) {
        out->setChecked(edge == edit::Edge::out);
    }
}

}  // namespace rf::app
