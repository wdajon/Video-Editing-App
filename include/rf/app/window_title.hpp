// Window title composition.
//
// Kept out of MainWindow and free of Qt so it can be tested without a display
// server, and so the Premiere-matching format lives in one place rather than
// being reassembled at each call site that marks the project dirty.

#ifndef RF_APP_WINDOW_TITLE_HPP
#define RF_APP_WINDOW_TITLE_HPP

#include <string>
#include <string_view>

namespace rf::app {

/// Composes the title bar text, matching Premiere Pro's convention:
///
///   no project open      -> "ReelForge"
///   project open         -> "ReelForge - C:\edits\promo.rfproj"
///   project with changes -> "ReelForge - C:\edits\promo.rfproj *"
///
/// An empty `project` means no project is open; `modified` is ignored in that
/// case, because there is nothing to have modified. Callers with an unsaved new
/// project pass its display name (e.g. "Untitled"), not an empty string -- this
/// function never invents a name for a project it was not given.
[[nodiscard]] std::string compose_window_title(std::string_view application,
                                               std::string_view project,
                                               bool modified);

}  // namespace rf::app

#endif  // RF_APP_WINDOW_TITLE_HPP
