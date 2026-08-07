# ADR 016 — Adobe's actual defaults, and putting the mouse back

- **Status:** Accepted
- **Date:** 2026-08-06
- **Milestone:** M4

## Context

Two reports from the project owner, and both were right.

**"The tools in the tool section do not work."** They effectively did not. Slip
and slide required selecting a tool and *then* pressing a trim chord — two
keystrokes before anything moved, with the first changing nothing visible.

**"This is also why I didn't want an only-keyboard video editor."** M4's exit
gate is *the full trim set driven by keyboard only*, and that phrasing quietly
became the whole design. A keyboard-only NLE is not a usable NLE. The gate was a
floor, not a ceiling, and reading it as a ceiling was a mistake.

They also asked, three times, that Adobe's default-shortcuts page be the source.

## Decision 1: the page was read, and it changes the bindings

Two `WebFetch` attempts timed out on that page, and I twice reported it as
unavailable and carried on with what earlier searches had suggested. That was
wrong: a browser was available the whole time. Loaded in it, the page reads
fine.

Source: <https://helpx.adobe.com/premiere/desktop/get-started/keyboard-shortcuts/default-keyboard-shortcuts.html>,
read 2026-08-06, **Timeline panel** and **Sequence menu** sections.

| Command | Windows default |
|---|---|
| Nudge Clip Selection Left / Right One Frame | `Alt+Left` / `Alt+Right` |
| Nudge Clip Selection Left / Right Five Frames | `Alt+Shift+Left` / `Alt+Shift+Right` |
| Slip Clip Selection Left / Right One Frame | `Ctrl+Alt+Left` / `Ctrl+Alt+Right` |
| Slip Clip Selection Left / Right Five Frames | `Ctrl+Alt+Shift+Left` / `Ctrl+Alt+Shift+Right` |
| Slide Clip Selection Left / Right One Frame | `Alt+,` / `Alt+.` |
| Slide Clip Selection Left / Right Five Frames | `Alt+Shift+,` / `Alt+Shift+.` |
| Step Backward / Forward | `Left` / `Right` |
| Play | `Space` |
| Undo / Redo | `Ctrl+Z` / `Ctrl+Shift+Z` |

**The correction that matters: slip and slide operate on the clip selection
directly.** No tool mode, one chord, something moves. ReelForge now binds exactly
these, and they work whatever tool is active.

**What the page does not assign**, checked rather than assumed: there is no
Timeline-panel *Trim Backward* / *Trim Forward*. `Ctrl+Left` / `Ctrl+Right`
appear on that page only under the **Program Monitor**, as *Nudge Selected Object*
— not as a trim. So ReelForge's ripple and roll trim keys are its own, and are
now labelled as such rather than implied to be Premiere's. Ripple and roll keep
the tool-plus-edge model because that genuinely is how Premiere reaches them,
through a tool and a selected edit point.

`[` and `]` for arming an edge remain ReelForge's invention (D26). The page
confirms Premiere uses them for *Set Work Area Bar In/Out Point* with `Alt`, so
the plain keys are free.

## Decision 2: the timeline takes a mouse

Clicking selects a clip. Dragging a clip moves it. Dragging in the ruler moves
the playhead. That is the minimum for the panel to be an editor rather than a
demonstration.

Three things this had to get right:

- **A drag is one undo entry.** The clip follows the pointer during the drag,
  and a single `move_clip` command is executed on release, from the original
  position to the final one. Committing per mouse-move would fill the history
  with hundreds of steps and make undo useless.
- **A drag that ends where it started does nothing.** No command, no undo entry,
  no modified flag — a click that wobbles is a click.
- **An illegal drop is refused, not clamped.** Dropping a clip onto another
  leaves it where it was and says so. Clamping to the nearest legal position
  would move the clip somewhere the user did not point at.

Dragging moves the whole link group, so a picture and its sound stay together,
for the same reason a keyboard nudge does.

## Decision 3: the palette shows pressed state properly

`setAutoRaise(true)` renders a `QToolButton` flat, and a flat checked button is
nearly indistinguishable from an unchecked one in the default style — so
selecting a tool looked like nothing happened even though it had. Auto-raise is
off, and the shortcut is separated from the label with spaces rather than a tab,
which `QToolButton` does not expand.

## Consequences

- The M4 gate's phrasing stands, but the milestone is no longer *only* keyboard.
  Mouse editing is not in the gate and is shipped anyway, because the gate was a
  floor.
- Tool mode still exists and still matters for ripple and roll. Slip and slide no
  longer need it, which leaves the palette's tool buttons doing less than they
  did — correctly so.
- **Not implemented from this page:** zoom (`=`/`-`), snap (`S`), add edit
  (`Ctrl+K`), mark in/out (`I`/`O`), ripple delete (`Alt+Backspace`). Recorded so
  the absence is deliberate rather than an oversight.
