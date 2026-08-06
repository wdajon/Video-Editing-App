// Qt key events to ReelForge key chords.
//
// The one place Qt's key vocabulary meets ReelForge's. `rf_edit` knows nothing
// about Qt on purpose (ADR 012), so this is the seam, and it is a free function
// rather than a widget method so it can be tested with synthesised events
// instead of only through a widget that also paints and holds state.

#ifndef RF_APP_KEY_TRANSLATION_HPP
#define RF_APP_KEY_TRANSLATION_HPP

#include <QKeyEvent>

#include <optional>

#include "rf/edit/key.hpp"

namespace rf::app {

/// Translates a Qt key event.
///
/// Returns `std::nullopt` for a key ReelForge has no name for. Falling through
/// to some default would bind a key to an action the user did not ask for,
/// which is worse than the key doing nothing.
[[nodiscard]] std::optional<edit::KeyChord> to_key_chord(const QKeyEvent& event);

/// The Qt key code for `key`, or `Qt::Key_unknown`. Exposed so a menu or a
/// shortcut hint can show the same key the command map would match.
[[nodiscard]] int to_qt_key(edit::Key key) noexcept;

}  // namespace rf::app

#endif  // RF_APP_KEY_TRANSLATION_HPP
