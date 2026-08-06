# ADR 009 — The trim set, and the media limit that makes it honest

- **Status:** Accepted
- **Date:** 2026-08-06
- **Milestone:** M4

## Context

M4's exit gate is *the full trim set (ripple/roll/slip/slide) driven by keyboard
only*. Two things have to exist before a key can be bound to anything: the four
operations themselves, and a definition of where each one is allowed to stop.

The second half is where the design pressure is. Every one of the four either
consumes source material that may not exist, or moves a neighbour that may not
have room. A trim that runs past the end of its media produces a clip whose
frames cannot be decoded — the model equivalent of the placeholder frame the
mission forbids. Today the document cannot even detect it: `Clip` records
`source_in` and `duration`, and nothing anywhere says how much media the source
actually has.

So this ADR settles three things: how much a clip knows about its media, what
each of the four operations does exactly, and how a compound edit stays atomic.

## Decision 1: a clip records the length of its source

`Clip` gains `Ticks source_duration` — the extent of usable media, measured from
source tick 0 in document ticks. The document enforces, on every mutation:

```
0 <= source_in  and  source_in + duration <= source_duration
```

That single invariant is what makes all four trim limits computable, and it makes
an over-trim impossible to represent rather than merely unlikely: a bug in the
trim arithmetic is refused by `Document`, not written into the timeline.

`add_clip` takes it as a **required trailing parameter**. Trailing, so that every
existing call site fails to compile rather than silently reinterpreting an
argument it already passes; required, because a default would be a guess about
media the caller has and the document does not.

Rejected alternatives:

- **A media pool the clip points into.** Where this belongs eventually, and where
  OpenTimelineIO puts it (`available_range` on the media reference, with
  `source_range` required to sit inside it). ReelForge has no media pool yet —
  `Clip::source` is a path string — and inventing one to hold a single field
  would be the larger and less reversible change. Recorded as a backlog item; the
  invariant above is exactly the one a pool would enforce, so it moves intact.
- **Passing the limit to the trim functions instead.** Keeps `Clip` unchanged and
  lets the document hold a clip that references frames that do not exist. The
  invariant has to live where the mutation happens or it is not an invariant.
- **A sentinel meaning "unknown, unbounded".** Every clip created without a real
  probe would take it, slip would be unbounded, and the failure would surface as
  a black frame during playback rather than as a refused edit.

**Consequence:** the project format goes to version 2, gaining a field on the
`clip` record. There is no reader yet, so no migration is written — but a v1 file
is missing a field that has no safe default, which is precisely why the version
number was put in the format on the first commit.

**Consequence:** media of genuinely unknown length (a growing file, a live
source) cannot be placed on the timeline. That is correct for now: nothing in
ReelForge can produce such a clip, since `probe` reports a duration.

## Decision 2: what each operation does, stated exactly

Ambiguity here is not academic — "ripple trim the in point" has two readings that
differ in whether the clip's own start moves, and they disagree about where the
new head material appears. Written out so the tests can be read against a
definition rather than against the implementation.

`C` is the clip being trimmed, `d` the signed delta in ticks, `prev`/`next` its
immediate neighbours on the same track. All four are **single-track**
operations; synchronised rippling across tracks is a later feature.

**Ripple, out edge.** `C.duration += d`, and every clip that starts after `C`
shifts by `d`. The sequence gets longer or shorter by `d`.

```
before   [ A ][   C   ][ B ][ D ]
after +2 [ A ][    C    ][ B ][ D ]      B and D both shift right by 2
```

**Ripple, in edge.** `C.source_in += d`, `C.duration -= d`, `C.start` unchanged,
and every clip after `C` shifts by `-d`. `C` keeps its start because the material
removed is at the head: the content slides so the new in point lands on
`C.start`, and the clip ends `d` earlier.

```
before   [ A ][   C   ][ B ]
after +2 [ A ][ C   ][ B ]               C loses 2 at the head, B shifts left by 2
```

**Roll.** Requires `C` and `next` to be butt-joined. `C.duration += d`;
`next.start += d`, `next.source_in += d`, `next.duration -= d`. Only the shared
edit point moves — the sequence length and both outer edges are unchanged.

**Slip.** `C.source_in += d`. `start` and `duration` are unchanged, no neighbour
is touched, and the sequence is unchanged. Slip is the only operation whose
effect is invisible in the timeline layout and visible only in the picture.

**Slide.** `C.start += d`. A butt-joined `prev` absorbs it with
`prev.duration += d`; a butt-joined `next` absorbs it with `next.start += d`,
`next.source_in += d`, `next.duration -= d`. `C`'s own `source_in` and `duration`
never change. Where a neighbour is not butt-joined, the gap absorbs the slide
instead and that neighbour is untouched — which is what a user sees when they
slide a clip that has empty space beside it.

Every limit follows mechanically from those definitions plus three constraints:
a clip's duration stays positive, Decision 1's media invariant holds, and a
clip's end stays representable. `trim_range()` returns the exact reachable
interval, and is public because the UI needs the same numbers to stop a drag at
the right pixel.

The third constraint is worth naming. A clip with nothing after it looks as
though it can slide right forever, and an earlier draft of `TrimRange` said so
with an optional bound meaning "unbounded". It was wrong twice over: a tick is an
`std::int64_t`, so the limit is real rather than absent, and leaving it out let
`start += delta` overflow for a delta the range had declared legal. `Document`
now refuses a clip whose `start + duration` is not representable, which makes the
bound derivable, and every range is a plain pair of integers with nothing to
saturate.

**Clamping, not refusal.** A requested delta larger than the range is clamped and
the operation applies as far as it can, which is what Premiere does. A request
that clamps to zero is refused with an error, so it never becomes an undo entry
for an edit that did not happen.

## Decision 3: a compound trim rewrites a whole track at once

Three of the four move more than one clip, and a half-applied trim cannot be
undone. Sequencing the existing primitives is possible but fragile — shifting
clips one at a time transiently overlaps unless the order is chosen per sign of
`d`, and a failure part-way needs hand-written rollback.

Instead, `Document::replace_track_clips()` takes a whole new clip vector for one
track, validates it completely, and installs it only if every check passes.
Commands built on it capture the previous vector and revert by putting it back,
so atomicity and exact inversion are properties of the primitive rather than
promises each command has to keep.

The primitive requires the incoming vector to carry **exactly the same clip ids**
as the current one. It is a rearrangement, not a creation: forbidding id changes
keeps the id counter out of it entirely, which is what keeps ADR 005's
byte-identity guarantee intact without any counter bookkeeping in these commands.

**Consequence:** a trim's undo record is one copy of a track's clip vector. For a
500-clip track that is a few tens of kilobytes per undo step. Acceptable now;
if it ever is not, the fix is a diff, and the primitive's shape does not have to
change for that.
