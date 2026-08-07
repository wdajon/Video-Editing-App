# ADR 017 — The Tools strip is a tool chooser, not a menu

- **Status:** Accepted
- **Date:** 2026-08-06
- **Milestone:** M4

## Context

ADR 015 put every command on a button so a user could see what the keys were.
A screenshot from the project owner showed what that actually produced: a
full-width column of two dozen buttons that expanded to fill the window, with the
Timeline squeezed behind it and clip names overlapping the button labels. The
panel had eaten the application.

The request was specific and correct: *"You don't need all of the tools displayed
on the screen. You just need the main common tools, like from the photo I showed
you earlier, with the ability to click and hold down one of those buttons and
[display] a separate drop-down menu off to the side for different options."*

## Decision 1: the strip holds tools; the menu bar holds commands

`ToolPalette` is a fixed-width column of tool icons. Everything that is not a
tool — trim, nudge, slip, slide, selection movement, transport, undo — moved to
the menu bar, under Clip, Sequence, Playback and Edit, which is where Premiere
keeps them and where a shortcut can be read off a menu entry.

The width is fixed, not merely preferred. A strip with an expanding size policy
is what swallowed the window, and a comment asking the next person not to do that
again is weaker than a layout that cannot.

## Decision 2: tools that share a slot share a button, with a flyout

Premiere groups ripple with rolling, and slip with slide, behind one button that
shows whichever you used last. Clicking uses it; clicking and holding opens a
flyout to change it.

Qt has this exactly: `QToolButton::DelayedPopup`. No custom press-and-hold timer,
no reimplementation of a menu.

A key press has to move the slot too — pressing `N` while the slot shows the
ripple icon must leave it showing rolling, or the strip would claim one tool is
active while displaying another.

## Decision 3: only tools that do something

Selection, ripple, rolling, slip and slide. Razor, rate stretch, pen, hand, zoom
and type are **absent**, not present and inert. M0's rule about panels that show
nothing applies to buttons that do nothing, and a tool strip is exactly where an
inert button is most convincing.

## Decision 4: the icons are drawn, not shipped

Five shapes of a few lines each, painted into a `QPixmap` at construction: an
arrow for selection, bars and arrows for the edit-point tools. No image files to
keep in step with the build, no licence question about resembling Adobe's set,
and they follow the same visual grammar so the strip is readable.

## Decision 5: the Timeline is the central widget

A `QMainWindow` with no central widget gives the leftover space to nothing, which
left a dead band across the window with the Timeline pinned into a strip at the
bottom. The Timeline is the panel always on screen in an editor, so it takes the
centre and the Tools strip docks beside it.

Revisit when the Program monitor can be embedded (D25) and the centre has a
rival for the space.

## Consequences

- Workspaces now save and restore the Tools dock rather than the Timeline dock.
  Still a real layout, still Qt's own state, and thinner than it was — with one
  dock, a workspace does not arrange much (D22 stands).
- `reelforge --screenshot <file>` was added to render the window and exit. Two
  layout defects in a row reached the owner because nothing in the test suite can
  see that a panel has covered the application. This is the mechanical half of
  looking at it, and it works on the offscreen platform.
