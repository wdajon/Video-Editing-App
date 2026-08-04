# ADR 005 — Timeline time representation, and how undo works

- **Status:** Accepted
- **Date:** 2026-08-04
- **Milestone:** M2

## Context

M2 is the timeline data model and the undo/redo command stack. Its exit gate is
a 10,000-operation fuzz in which undoing everything must return the document to a
**byte-identical** state. That phrasing settles more design than it first appears:

- Byte-identity is measured on a serialised document, so a canonical
  serialisation is part of the model, not a later feature.
- Every command's inverse must restore *everything*, including identity and
  ordering. An inverse that reconstructs a clip with the same fields but a new
  id, or reinserts it at a different index, passes a shallow equality check and
  fails a byte comparison. The gate is designed to catch exactly that.

## Decision 1: time is integer ticks in one document-wide base

The document carries a single `rf::media::Rational time_base`. Every position and
duration in the timeline is an `std::int64_t` count of those ticks.

Rejected alternatives:

- **Seconds as `double`.** Rejected for the reason `Rational` exists at all: 
  1001/30000 is not representable, and a timeline is thousands of additions.
- **`Rational` seconds per value.** Exact, but every clip boundary would then be
  a pair needing normalisation, comparisons could overflow, and two documents
  that are semantically identical could serialise differently depending on how
  each value happened to be reduced. Byte-identity would become a property of
  arithmetic history rather than of state.
- **A per-object rate (OpenTimelineIO's `RationalTime`).** Better for
  interchange, and ReelForge must import and export OTIO eventually. But mixed
  rates inside one document mean every comparison is a rescale that can fail,
  and the fuzz gate would be testing rescale arithmetic rather than the command
  stack. Conversion happens at the OTIO boundary instead.

Integers in one base give exact arithmetic, total ordering with no allocation,
and a serialisation with exactly one spelling per value.

## Decision 2: undo is command inversion, not snapshots

Each command carries what it needs to apply itself and to invert itself exactly.
The stack holds commands; undo walks backwards applying inverses.

Rejected alternatives:

- **Whole-document snapshots.** Trivially byte-identical and trivially correct,
  and it is what makes the gate easy to pass without the design being right. A
  500-clip project would copy the entire document per keystroke, which the M2
  performance budget (project open ≤ 2 s, and later a 30 fps scrub) will not
  tolerate.
- **Persistent immutable tree with structural sharing.** Undo becomes a root
  pointer swap and byte-identity is free. Genuinely attractive, and if this were
  a greenfield language choice it would win. Rejected here because the brief
  specifies a command stack, because every UI action then needs a rebuild path,
  and because debugging a shared-structure aliasing bug in C++ is markedly worse
  than debugging a bad inverse.

The cost is accepted deliberately: **a wrong inverse is a silent data-loss bug**,
and it is the single most likely defect in this milestone. That is what the
10,000-operation fuzz exists to find, and why the gate compares bytes rather
than calling `operator==`.

## Decision 3: identity is explicit and stable

Clips and tracks carry strongly-typed ids (`ClipId`, `TrackId`) issued by the
document from a monotonic counter that is itself part of the document state.

This matters for undo specifically. If deleting the last clip decremented the
counter, a redo would reuse the id and two different histories would produce the
same bytes for different documents. The counter therefore only ever moves
forward, and its value is serialised: restoring a document restores its notion of
what ids have been spent.

Ids are strong types rather than `int` because a timeline is full of integers —
indices, ticks, track numbers — and passing a track index where a clip id is
expected must not compile.

## Decision 4: the serialisation is canonical, versioned, and diffable

One document state has exactly one byte representation: fields in a fixed order,
collections in a defined order, integers with no alternative spellings, and no
locale-dependent formatting anywhere.

The format carries a schema version from the first commit. The brief calls an
unversioned project file a P0 bug, and it is right: a format without a version is
a format that can never be changed safely.

## Consequences

- `rf_timeline` depends on `rf_core` and on `rf::media::Rational`. It does not
  depend on libav, Qt, or the GPU, and is fully testable headlessly.
- Adding a command means adding its inverse and a fuzz entry in the same commit.
  A command absent from the fuzz is a command whose inverse is unverified.
- Byte-identity is asserted on serialised output, so serialisation defects and
  inverse defects are both caught by the same gate.
- Interchange with OpenTimelineIO converts at the boundary; the internal model
  stays single-base.
