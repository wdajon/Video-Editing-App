# ADR 012 — The command map, and trimming in frames

- **Status:** Accepted
- **Date:** 2026-08-06
- **Milestone:** M4

## Context

M4's exit gate is *the full trim set driven by keyboard only*. The operations
exist (ADR 009), they keep other tracks in step (ADR 010) and they keep a clip's
audio with its picture (ADR 011). None of them is reachable without calling a C++
function with a clip id and a tick count, which is not a workflow.

Three things stand between here and the gate: a way to name a key, a way to bind
a key to an editing action, and enough editor state — which tool, which clip,
which edge — for an action to know what it acts on. This ADR settles all three,
plus one prerequisite that turned up while sizing the work.

## Decision 1: the document carries a frame rate

Nobody trims by ticks. A tick is 1/90000 s; Premiere's trim keys move one frame,
or several. So a trim key has to convert frames to ticks, and to do that the
document has to know its frame rate — and it does not. ADR 005 gave it a tick
base, which is a *resolution*, not a rate.

`Document::create()` now takes both, and refuses a frame rate whose period is not
a whole number of ticks. `ticks_per_frame()` is then exact, and every keyboard
trim lands on a frame boundary by construction rather than by rounding.

The refusal is a real constraint and worth stating plainly: at the conventional
1/90000 base, 30, 25, 24 and 29.97 fps are all exact, and **23.976 fps
(24000/1001) is not** — 90000 × 1001 / 24000 is 3753.75. A document at that rate
cannot be created at that base today. The alternative was to round, which puts
a fraction of a frame of drift into every trim and is precisely the class of
error the integer tick model exists to prevent. Choosing a base that expresses
every broadcast rate exactly — flicks, 1/705600000, designed for this — is
recorded as follow-up rather than done here, because it touches every existing
document.

## Decision 2: a key chord is a value, and the map is a data file

`KeyChord` is a `Key` plus a modifier mask, comparable and hashable, with no Qt
in sight. `rf_edit` is testable with no window, which is what lets the whole
keyboard workflow be a headless test rather than a screenshot.

The map itself is a **plain-text, versioned, line-oriented file**, the same shape
as the project format and for the same reasons: a keyboard layout is something
users diff, share and review. It is not JSON, and that is not an oversight — the
mission requires *delivery presets* to be JSON validated against a schema, which
is a different file with a different audience. Adding a JSON dependency for M5's
presets remains open and does not have to make this file follow suit.

A built-in default map is compiled in, so ReelForge is usable with no file
present, and any binding can be overridden by a loaded map. Remapping is
therefore additive rather than all-or-nothing.

## Decision 3: a tool plus a selection, as Premiere does it

Premiere's keyboard trim is modal: pick a tool, select an edit point, then press
a trim key. `EditState` holds exactly that — the active `Tool`, the selected clip
and edge, and the playhead — and `Editor` turns an `Action` into a command
against the document.

The defaults follow Premiere's, which were read from Adobe's documentation rather
than remembered:

| Binding | Action | Premiere |
|---|---|---|
| `V` `B` `N` `Y` `U` | select tool: selection, ripple, roll, slip, slide | same |
| `Ctrl+Left` / `Ctrl+Right` | trim backward / forward one frame | same |
| `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | trim by the large trim offset | same |
| `Up` / `Down` | select previous / next clip | Premiere selects the previous/next **edit point** |
| `[` / `]` | act on the clip's in / out edge | **no Premiere default** |

The last two are where ReelForge differs, and both are stated rather than
smuggled. Premiere has commands for *Select Nearest Edit Point as Ripple In* and
*…as Ripple Out* but ships them unassigned, so a keyboard-only user cannot choose
an edge out of the box. The gate requires that they can, so ReelForge assigns
them. If that turns out to fight muscle memory, it is one line in a keymap file,
which is the point of having one.

**Large trim offset defaults to 5 frames**, Premiere's default. It lives in
`EditState` as a settable field, not as a literal in the trim path.

## Decision 4: an action that does not apply is an error, not a no-op

Pressing a trim key with the selection tool active, or with nothing selected,
returns an error naming what is missing. The alternative — doing nothing quietly
— is indistinguishable from a broken key binding, and a keyboard-only user has
nothing else to go on.

## Consequences

- The project format goes to version 5 for the frame rate.
- `rf_edit` depends on `rf::timeline` and `rf::core`, and on nothing else. It
  does not know Qt exists; M4's remaining Qt work will call into it.
- JKL shuttle is **not** in this layer yet. It drives playback rather than the
  document, so it belongs with the Qt playback wiring, and it is the one part of
  M4's title still untouched.
