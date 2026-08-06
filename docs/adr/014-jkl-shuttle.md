# ADR 014 — JKL shuttle, and what it is honestly connected to

- **Status:** Accepted
- **Date:** 2026-08-06
- **Milestone:** M4

## Context

JKL is the last item in M4's title with nothing behind it (D21). J plays
backward, K stops, L plays forward, and repeated presses shuttle faster. It is
the control every editor reaches for before any other, and the reason a keyboard
editor feels like an instrument rather than a form.

It is also the item most at risk of being faked. A shuttle that changes a number
nobody reads would pass a test suite and do nothing, so this ADR settles what it
is connected to as carefully as it settles what it does.

## Decision 1: the ladder is data, and the sources disagreed

Sources read 2026-08-06:

- <https://helpx.adobe.com/premiere/desktop/get-started/keyboard-shortcuts/default-keyboard-shortcuts.html>
- <https://nofilmschool.com/2018/10/how-use-j-k-and-l-keys-premiere-pro-speed-your-workflow>
- <https://community.adobe.com/t5/premiere-pro-ideas/expand-shuttle-speed-options-jkl/m-p/14675392>

They agree that J and L shuttle backward and forward, that K stops, and that
repeated presses go faster. They **disagree on the ladder** — doubling
(1, 2, 4, 8) appears alongside linear (1, 2, 3, 4), and the Shift behaviour is
described variously as slow motion and as a finer step.

The mission's rule for disagreeing sources is to record all values, implement the
most conservative, and put the constraint behind configuration rather than a
magic number. So the ladder is a `std::vector<Rational>` on the shuttle,
defaulting to **1, 2, 4, 8, 16** — doubling, which the largest number of sources
describe and which is what "shuttle" means on a physical jog wheel. A user or a
future preferences page replaces the vector; nothing in the transition logic
knows how many rungs there are or what they contain.

## Decision 2: the opposite key steps one rung, it does not jump

Pressing J while playing forward steps **one rung toward zero**, reaching stopped
at 1x and reversing only on the next press. The alternative — jumping straight to
reverse 1x — is what some descriptions of Premiere suggest, and it is the less
conservative reading: a single keystroke that reverses direction at speed is
hard to undo by feel, and this is a control operated without looking.

Ramping down is also what a jog wheel does, which is the thing JKL imitates.

## Decision 3: the rate is a rational, so slow motion is exact

`Shuttle::rate()` returns a `media::Rational`, signed, with zero meaning stopped.
Half speed is 1/2, not 0.5. `PlaybackClock` already takes a rational rate and
already accepts a negative one, so reverse and slow motion need nothing new from
it — which is a good sign the M3 clock was designed at the right shape.

## Decision 4: what it is connected to, stated exactly

`Shuttle` lives in `rf_edit` and knows nothing about playback. `rf_app::Transport`
owns a `PlaybackClock` and applies a rate to it. The application layer joins
them, so `rf_edit` does not gain a dependency on `rf_playback` and the join is
testable with a `ManualClock` rather than by waiting.

The chain that now exists, end to end:

```
key press -> KeyChord -> Action -> Shuttle rate -> PlaybackClock -> playhead
          -> TimelinePanel draws the playhead where the clock says it is
```

**The chain that does not exist:** the playhead moving does not move any picture.
The Program monitor owns a Vulkan surface and is deliberately outside the widget
tree (ADR 013), so nothing decodes or presents at the shuttle rate. Pressing L
sweeps a playhead across the Timeline; it does not play video.

That is a real limit and it is written here rather than left to be discovered.
Connecting the monitor needs a device, which a CI runner cannot provide, and it
would put the one untestable component inside the tree these tests walk. It is
filed rather than faked.

## Consequences

- `EditState` gains a `Shuttle`, and — with a reader at last — a playhead frame.
  The playhead was removed in ADR 012 for being written by selection and read by
  nothing; it comes back here because the transport gives it a meaning.
- Shift+J and Shift+L are bound to slow motion. Premiere's own Shift behaviour is
  among the disputed points above, so this is the reading implemented, not a
  claim about what Premiere does.
- Nothing sleeps or spins: the clock is asked where the playhead is when the
  window repaints, exactly as ADR 006 intends.
