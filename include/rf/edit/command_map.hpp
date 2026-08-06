// Which key does what, and how a user changes it.
//
// The default map is compiled in, so ReelForge works with no file present. A
// loaded map overrides individual bindings rather than replacing the whole
// thing, so a user who wants one key moved does not inherit responsibility for
// every other key. See docs/adr/012-command-map.md.

#ifndef RF_EDIT_COMMAND_MAP_HPP
#define RF_EDIT_COMMAND_MAP_HPP

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "rf/core/result.hpp"
#include "rf/edit/action.hpp"
#include "rf/edit/key.hpp"

namespace rf::edit {

/// Schema version of a keymap file. A reader that does not recognise a version
/// refuses the file rather than guessing, exactly as the project format does.
inline constexpr int kKeymapFormatVersion = 1;

class CommandMap {
public:
    /// Premiere's defaults where Premiere has one; see ADR 012 for the two
    /// bindings that differ and why.
    [[nodiscard]] static CommandMap defaults();

    /// Parses a keymap document, starting from `defaults()` and applying each
    /// `bind` line over the top.
    [[nodiscard]] static Result<CommandMap> parse(std::string_view text);

    /// Parses a keymap with nothing underneath it. For a user who wants the
    /// file to be the whole truth.
    [[nodiscard]] static Result<CommandMap> parse_standalone(std::string_view text);

    [[nodiscard]] std::optional<Action> lookup(const KeyChord& chord) const;

    /// Every chord bound to `action`, in the order they were bound. An action
    /// may have several, and a UI that shows shortcuts needs them all.
    [[nodiscard]] std::vector<KeyChord> chords_for(Action action) const;

    /// Binds `chord`, replacing whatever it did before. Binding an action to a
    /// second chord does not unbind the first.
    void bind(const KeyChord& chord, Action action);

    /// Removes a binding. Returns false if the chord was not bound.
    bool unbind(const KeyChord& chord);

    [[nodiscard]] std::size_t size() const noexcept { return bindings_.size(); }

    /// Serialises canonically: sorted by chord spelling, so two equal maps
    /// produce identical bytes and a diff of two keymaps shows only real
    /// differences.
    [[nodiscard]] std::string serialise() const;

private:
    std::unordered_map<KeyChord, Action> bindings_;
};

}  // namespace rf::edit

#endif  // RF_EDIT_COMMAND_MAP_HPP
