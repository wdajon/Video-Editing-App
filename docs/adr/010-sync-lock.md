# ADR 010 — Sync lock, and what it is not responsible for

- **Status:** Accepted
- **Date:** 2026-08-06
- **Milestone:** M4

## Context

D15 was raised at the end of M4 iteration 1 in these words: *"a ripple trims one
track, so a keyboard ripple on V1 leaves the paired A1 clip behind and the edit
goes out of sync."* Checking Adobe's documentation before implementing it showed
that sentence conflates two separate features, and building one of them under the
other's name would have produced something that looked finished and still lost
sync.

Adobe's page on sync lock says clips on a sync-locked track stay in sync with a
track being ripple trimmed or inserted into, and that a track containing a clip
that is *part of* the operation shifts regardless of its own sync-lock state. The
scope is **downstream material on other tracks** — the music bed on A2, the lower
third on V2 — moving by the same amount so it stays where it was relative to the
picture.

The paired A1 clip is a different mechanism entirely: in Premiere the audio and
video of one source are **linked**, and a trim with linked selection trims both.
Sync lock never trims anything; it only shifts.

Sources, read 2026-08-06:

- <https://helpx.adobe.com/premiere/desktop/edit-projects/change-clip-sequence/sync-lock-to-prevent-changes.html>
- <https://community.adobe.com/announcements-732/now-in-beta-ripple-trim-adds-edits-to-keep-both-sides-of-trim-in-sync-313516>

## Decision 1: sync lock shifts, and only a ripple triggers it

`Track` gains `bool sync_locked`, defaulting to **true**, which is Premiere's
default. A ripple then rewrites the trimmed track *and* every sync-locked track.

Roll, slip and slide are untouched by this. None of them changes the length of
the sequence, so there is no downstream material to move; adding them to the
sync-lock path would move clips for no reason.

The trimmed track shifts whether or not its own sync lock is set, matching the
rule that a track holding a clip in the operation is part of the operation.

## Decision 2: the ripple point is the clip's out point, for both edges

A ripple shifts every clip whose `start` is at or after a point `P`, by a signed
amount. For a ripple of the out edge the amount is `+d`; for the in edge it is
`-d`. In both cases:

```
P = the trimmed clip's out point, before the trim
```

That is obvious for the out edge and not obvious for the in edge, so it is worth
deriving. Rippling the in point by `+d` takes `d` ticks off the head of the clip
while the clip keeps its start (ADR 009), so the clip's *end* retreats by `d` and
everything from that end onward closes up. The point where the sequence contracts
is therefore the out point, not the in point, even though the material removed
came off the head.

One rule for both edges is worth having: the alternative is two ripple points
that differ by the clip's duration, and any disagreement between them shows up as
a silent frame-level drift on the other tracks rather than as an error.

## Decision 3: a clip straddling the ripple point refuses the whole edit

If a sync-locked track has a clip that begins before `P` and ends after it, there
is no shift of that track that keeps it legal: the clip cannot move (its head is
anchored by material before the point) and cannot stay (its tail is in the region
being moved).

Three options:

- **Refuse the ripple**, naming the track and the clip. Chosen.
- **Leave that clip and shift the ones after it.** This is the failure D15
  described. It silently opens or closes a gap around the clip and desyncs
  everything downstream of it on that track.
- **Split the clip at `P` and ripple both halves.** What Premiere added, behind
  the preference *"Ripple trim adds edits to keep both sides of trim in sync"* —
  and the fact that it arrived as an opt-in preference rather than as the default
  says how much it changes an edit. It also creates clips, so it spends ids and
  needs a different undo shape from every other trim.

Refusing is the only one of the three that cannot lose sync silently. The error
names the obstruction, so the user can drop that track's sync lock and repeat the
edit. Splitting is recorded as follow-up work rather than guessed at now.

## Decision 4: one primitive rewrites several tracks at once

`Document::replace_track_clips()` from ADR 009 is per-track, and a ripple across
four sync-locked tracks that validated and installed them one at a time could
fail on the fourth having already changed three. `Document::replace_clips()` takes
a rewrite for each affected track, validates all of them, and installs them only
if every one is legal. `replace_track_clips()` is now a one-element call into it,
so the two cannot disagree about what is valid.

## Consequences

- The project format goes to version 3 for `Track::sync_locked`.
- `insert_track_at()` took seven positional parameters and would have taken
  eight. It now takes a `Track` by value, which is what its only caller — undo
  restoring a removed track — already had in hand.
- **Linked clips do not exist**, so a ripple on V1 still does not trim a paired
  clip on A1. That is the other half of D15 and is filed separately. Sync lock
  keeps the *rest* of the timeline aligned; it was never going to fix that, and
  this ADR exists partly so the next session does not read D15 as closed.
