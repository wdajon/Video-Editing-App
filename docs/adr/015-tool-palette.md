# ADR 015 — A palette of buttons, and why each one names its own key

- **Status:** Accepted
- **Date:** 2026-08-06
- **Milestone:** M4

## Context

The project owner ran `reelforge --demo-timeline` and reported, in substance:
*I don't know whether the shortcuts are working, or whether I'm doing them
right.* The sequence they were given — `B`, `]`, `Ctrl+Right` — reads as three
mystery keystrokes, and two of them change nothing you can see. Pressing `B`
selects the ripple tool and the window looked identical afterwards.

That is a real defect, not a documentation problem. A keyboard-first application
still has to be *learnable*, and every editor solves this the same way: a palette
of tools you can click, each telling you its key.

## Decision 1: a button performs an Action; it does not reimplement one

`ToolPalette` emits `action_triggered(Action)` and nothing else. The window wires
that to `TimelinePanel::perform()`, which is the same function a key press
reaches after the command map resolves a chord.

Two entry points that each interpreted an action would be two things to keep in
step, and the first divergence would be a button that quietly did something its
shortcut did not. `ToolPaletteTest.ClickingARippleButtonTrimsExactlyAsTheKeyDoes`
performs the same edit both ways and compares the serialised documents, so the
two cannot drift without a test failing.

## Decision 2: the label is read from the command map, never written beside it

A button's text comes from `CommandMap::chords_for(action)` at construction. A
remapped key relabels its own button, and a button can never advertise a
shortcut that does nothing.

The shortcut goes on the **face** of the button rather than only in a tooltip.
The complaint that produced this panel was not knowing whether a key had done
anything; a hint you have to hover to discover does not answer that.

## Decision 3: state that a key changes must be visible

Selecting a tool changes no clip, which is exactly why pressing `B` felt like
nothing. Two things now show it:

- The palette checks the active tool's button and the armed edge's button.
- The status bar carries a permanent label: the tool, the selected clip, and —
  for ripple and roll only — which edge. Slip and slide use the whole clip, so
  showing an edge for them would advertise a choice with no effect.

`TimelinePanel` emits `edit_state_changed()` after **every** action, successful
or refused, because a refused trim still leaves a tool and a selection worth
showing.

## Decision 4: clicking hands focus back to the timeline

Qt gives focus to a clicked button. Without correcting that, someone who used the
palette to discover a key and then pressed it would find the key did nothing —
precisely the confusion this panel exists to remove.

## On Premiere's shortcuts

The owner supplied Adobe's default-shortcuts page and asked that it be the
source. It could not be fetched: two attempts timed out, the page being very
large. Nothing was changed on the strength of a guess.

What ReelForge binds today was taken from Adobe pages earlier in M4 and recorded
in ADR 012 — `V`, `B`, `N`, `Y`, `U` for the tools, `Ctrl+←`/`Ctrl+→` for a
one-frame trim, `Ctrl+Shift+` for the large offset, `J`/`K`/`L` for the shuttle.
Those already match Premiere.

The exceptions remain `[` and `]` for arming an edge, which Premiere has no
default for (ADR 012 decision 3). They are now buttons with visible labels, so
they are discoverable rather than secret — but they are still ReelForge's
invention, and re-checking them against Adobe's page is unfinished work rather
than a settled question.

## Consequences

- A second dockable panel exists, so a workspace now arranges something.
- `MainWindowWorkspaces.DoesNotShipEmptyPanelsStandingInForRealOnes` asserted
  "exactly one dock" and failed the moment a second real panel arrived — the
  opposite of what it guards. It now asserts that every dock has content and a
  stable object name, which is what M0's rule actually says.
