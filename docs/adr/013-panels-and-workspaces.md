# ADR 013 — Panels, workspaces, and getting a real key press to a real edit

- **Status:** Accepted
- **Date:** 2026-08-06
- **Milestone:** M4

## Context

`rf_edit` (ADR 012) proves the whole trim set is reachable from key chords, with
no window anywhere near it. That is the right shape and it is not the gate. The
gate says *driven by keyboard only*, and a user drives a window, not a
`CommandMap`. What is missing is the last hop: a `QKeyEvent` arriving at a widget
that has focus, becoming a `KeyChord`, and coming out as an edit.

M0 left a deliberate note in `MainWindow`: *"there are deliberately no empty dock
widgets standing in for them, because a panel that docks but shows nothing is
indistinguishable from a broken panel."* That constraint still holds and shapes
what this iteration is allowed to build.

## Decision 1: translation is a free function, not a widget method

`to_key_chord(const QKeyEvent&)` lives on its own and returns
`std::optional<KeyChord>`. A key ReelForge has no name for returns `nullopt`
rather than a wrong chord — binding `Ctrl+Right` to something because a key
happened to fall through a `default:` case is worse than not binding it.

Being a free function is what makes it testable directly, with synthesised
events, rather than only through a widget that also paints and holds state. Every
key in the `Key` enumeration is asserted to survive Qt → `Key` → Qt.

**Qt's keypad and layout traps, handled explicitly:** Qt reports `Qt::KeypadModifier`
for the numeric keypad, which would otherwise make `Ctrl+Right` from the keypad a
different chord from `Ctrl+Right` from the arrow cluster. It is masked out. Only
Shift, Control and Alt survive into a chord, matching `Modifiers`.

## Decision 2: only panels with something to show

This iteration ships **one** new panel, the Timeline, because it is the only one
with content to draw: tracks, clips, the selection and which edge is armed. It
paints from the document rather than from a cached copy, so it cannot show a
timeline the document does not have.

Project, Source and Effect Controls are **not** created as empty docks. M0's rule
applies unchanged, and an empty Project panel would be a worse lie now than it
was then, because the surrounding window would make it look finished.

The Program monitor already exists as a `QWindow` owning a Vulkan surface
(ADR 008). It is deliberately *not* docked here: doing so needs a device, which a
CI runner with no GPU cannot provide, and it would put the one untestable part of
the application inside the widget tree that this iteration's tests walk.

## Decision 3: a workspace is Qt's own layout state, named

`QMainWindow::saveState()` returns the dock geometry as a `QByteArray` and
`restoreState()` puts it back. A workspace is that array under a name. ReelForge
does not invent a layout format.

Two consequences worth naming. Qt's state is keyed on `objectName`, so every dock
must have a stable one — a renamed dock silently loses its position, which looks
like a bug in the layout rather than in the name. And Qt versions its own format;
`restoreState` returns false rather than corrupting a window when it does not
recognise the data, and ReelForge propagates that instead of ignoring it.

## Decision 4: focus follows the panel, and the panel owns nothing

`TimelinePanel` holds references to the document, the command stack, the edit
state and the command map. It owns none of them. The window owns them, which is
what lets a test construct a panel around its own document and drive it, and what
will let a second panel act on the same document later without either of them
being the authority.

An action that fails puts its message on the panel's status signal rather than
into a dialog. A keyboard-only user pressing a trim key at the media limit needs
to know why nothing moved, and a modal dialog on every refused keystroke would be
unusable.

## Consequences

- The keyboard-only workflow is now testable end to end with `QTest::keyClick`
  against a real widget, on a machine with no display, via the offscreen QPA
  platform the app suite already uses.
- **JKL is still not implemented.** It drives playback, which means the Program
  monitor, which is the part deliberately left out above. It remains the one item
  in M4's title with nothing behind it.
- Docking exists with one dockable panel, which exercises the mechanism but does
  not demonstrate a workspace anyone would want. Honest state, recorded.
