#include "rf/app/tool_palette.hpp"

#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QToolButton>
#include <QVBoxLayout>

#include <string>

namespace rf::app {
namespace {

using edit::Action;

constexpr int kIconSize = 22;
constexpr int kButtonSize = 34;

[[nodiscard]] QString shortcut_text(const edit::CommandMap& map, Action action) {
    const std::vector<edit::KeyChord> chords = map.chords_for(action);
    if (chords.empty()) {
        return {};
    }
    return QString::fromStdString(edit::to_string(chords.front()));
}

[[nodiscard]] QString tool_name(Action action) {
    switch (action) {
        case Action::select_tool_selection: return QObject::tr("Selection Tool");
        case Action::select_tool_ripple:    return QObject::tr("Ripple Edit Tool");
        case Action::select_tool_roll:      return QObject::tr("Rolling Edit Tool");
        case Action::select_tool_slip:      return QObject::tr("Slip Tool");
        case Action::select_tool_slide:     return QObject::tr("Slide Tool");
        default:                            return {};
    }
}

/// Drawn rather than shipped as artwork: five shapes of a few lines each, and no
/// image files to keep in step with the build. They follow Premiere's grammar --
/// an arrow for selection, brackets for the edit-point tools -- so the strip is
/// recognisable without pretending to be Adobe's icon set.
[[nodiscard]] QIcon tool_icon(Action action, const QColor& ink) {
    QPixmap pixmap(kIconSize, kIconSize);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(ink);
    pen.setWidth(2);
    painter.setPen(pen);
    painter.setBrush(ink);

    const int mid = kIconSize / 2;
    switch (action) {
        case Action::select_tool_selection: {
            // A pointer.
            const QPolygon arrow({QPoint(6, 3), QPoint(6, 18), QPoint(10, 14),
                                  QPoint(13, 19), QPoint(15, 18), QPoint(12, 13),
                                  QPoint(17, 12)});
            painter.drawPolygon(arrow);
            break;
        }
        case Action::select_tool_ripple:
            // One fixed edge, one that moves: a bar and an arrow leaving it.
            painter.drawLine(5, 4, 5, 18);
            painter.drawLine(9, 11, 18, 11);
            painter.drawLine(18, 11, 14, 7);
            painter.drawLine(18, 11, 14, 15);
            break;
        case Action::select_tool_roll:
            // Two bars moving together.
            painter.drawLine(8, 4, 8, 18);
            painter.drawLine(14, 4, 14, 18);
            painter.drawLine(2, 11, 6, 11);
            painter.drawLine(16, 11, 20, 11);
            break;
        case Action::select_tool_slip:
            // Content sliding inside fixed edges.
            painter.drawLine(4, 4, 4, 18);
            painter.drawLine(18, 4, 18, 18);
            painter.drawLine(8, 11, 14, 11);
            painter.drawLine(8, 11, 11, 8);
            painter.drawLine(14, 11, 11, 14);
            break;
        case Action::select_tool_slide:
            // Fixed content, edges moving around it.
            painter.drawLine(9, 4, 9, 18);
            painter.drawLine(13, 4, 13, 18);
            painter.drawLine(2, mid, 6, mid);
            painter.drawLine(2, mid, 5, mid - 3);
            painter.drawLine(16, mid, 20, mid);
            painter.drawLine(20, mid, 17, mid - 3);
            break;
        default:
            break;
    }
    painter.end();
    return QIcon(pixmap);
}

}  // namespace

ToolPalette::ToolPalette(const edit::CommandMap& map, QWidget* parent)
    : QWidget(parent), map_(map) {
    setObjectName("rf_panel_tools");

    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(3, 3, 3, 3);
    layout_->setSpacing(2);

    // Premiere's grouping, minus the tools ReelForge does not have. Razor, pen,
    // hand, zoom and type are absent rather than present and dead -- M0's rule
    // about panels that show nothing applies to buttons that do nothing.
    add_slot({Action::select_tool_selection});
    add_slot({Action::select_tool_ripple, Action::select_tool_roll});
    add_slot({Action::select_tool_slip, Action::select_tool_slide});

    layout_->addStretch(1);

    // Fixed width, so the strip cannot expand and swallow the window -- which is
    // exactly what the previous full-width version did to the Timeline.
    const int width = kButtonSize + layout_->contentsMargins().left() +
                      layout_->contentsMargins().right();
    setFixedWidth(width);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    set_active_tool(edit::Tool::selection);
}

void ToolPalette::add_slot(const std::vector<Action>& tools) {
    auto* button = new QToolButton(this);
    button->setObjectName(QStringLiteral("rf_toolslot_%1")
                              .arg(QString::fromStdString(std::string(
                                  edit::to_string(tools.front())))));
    button->setCheckable(true);
    button->setAutoRaise(false);
    button->setIconSize(QSize(kIconSize, kIconSize));
    button->setFixedSize(kButtonSize, kButtonSize);

    if (tools.size() > 1) {
        // DelayedPopup is precisely "click to use, click and hold for the rest",
        // which is the behaviour Premiere has and the one that was asked for.
        auto* menu = new QMenu(button);
        for (const Action tool : tools) {
            auto* entry = menu->addAction(QString{});
            entry->setIcon(tool_icon(tool, palette().color(QPalette::ButtonText)));
            connect(entry, &QAction::triggered, this, [this, button, tool] {
                show_in_slot(button, tool);
                Q_EMIT action_triggered(tool);
            });
            entry_of_tool_.insert(static_cast<int>(tool), entry);
        }
        button->setMenu(menu);
        button->setPopupMode(QToolButton::DelayedPopup);
    } else {
        entry_of_tool_.insert(static_cast<int>(tools.front()), nullptr);
    }

    for (const Action tool : tools) {
        slot_of_tool_.insert(static_cast<int>(tool), button);
    }

    // Clicking uses whichever tool the slot is showing.
    connect(button, &QToolButton::clicked, this, [this, button] {
        Q_EMIT action_triggered(static_cast<Action>(button->property("rf_tool").toInt()));
    });

    show_in_slot(button, tools.front());
    layout_->addWidget(button);

    // Label the flyout entries once the button exists, so each carries the
    // shortcut read from the live command map rather than a written-in copy.
    for (const Action tool : tools) {
        if (QAction* entry = entry_of_tool_.value(static_cast<int>(tool), nullptr);
            entry != nullptr) {
            const QString shortcut = shortcut_text(map_, tool);
            entry->setText(shortcut.isEmpty()
                               ? tool_name(tool)
                               : QStringLiteral("%1 (%2)").arg(tool_name(tool), shortcut));
        }
    }
}

void ToolPalette::show_in_slot(QToolButton* button, Action tool) {
    button->setProperty("rf_tool", static_cast<int>(tool));
    button->setIcon(tool_icon(tool, palette().color(QPalette::ButtonText)));
    const QString shortcut = shortcut_text(map_, tool);
    button->setToolTip(shortcut.isEmpty()
                           ? tool_name(tool)
                           : tr("%1 (%2)\nClick and hold for more tools")
                                 .arg(tool_name(tool), shortcut));
}

QToolButton* ToolPalette::button_for(Action action) const {
    return slot_of_tool_.value(static_cast<int>(action), nullptr);
}

QAction* ToolPalette::menu_action_for(Action action) const {
    return entry_of_tool_.value(static_cast<int>(action), nullptr);
}

QString ToolPalette::label_for(Action action) const {
    QAction* entry = menu_action_for(action);
    if (entry != nullptr) {
        return entry->text();
    }
    QToolButton* button = button_for(action);
    return button == nullptr ? QString{} : button->toolTip();
}

void ToolPalette::set_active_tool(edit::Tool tool) {
    static const QHash<int, Action> actions{
        {static_cast<int>(edit::Tool::selection), Action::select_tool_selection},
        {static_cast<int>(edit::Tool::ripple), Action::select_tool_ripple},
        {static_cast<int>(edit::Tool::roll), Action::select_tool_roll},
        {static_cast<int>(edit::Tool::slip), Action::select_tool_slip},
        {static_cast<int>(edit::Tool::slide), Action::select_tool_slide},
    };
    const Action active = actions.value(static_cast<int>(tool), Action::select_tool_selection);
    QToolButton* active_button = button_for(active);

    for (auto it = slot_of_tool_.constBegin(); it != slot_of_tool_.constEnd(); ++it) {
        it.value()->setChecked(it.value() == active_button);
    }
    // A key press can pick a tool from a slot that is showing its sibling, so
    // the slot has to follow -- otherwise the strip would claim ripple is active
    // while displaying the rolling icon.
    if (active_button != nullptr) {
        show_in_slot(active_button, active);
    }
}

}  // namespace rf::app
