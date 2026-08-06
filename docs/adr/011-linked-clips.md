# ADR 011 — Linked clips, and why they must start aligned

- **Status:** Accepted
- **Date:** 2026-08-06
- **Milestone:** M4

## Context

D16 is the half of the original D15 report that sync lock was never going to
fix. In Premiere the audio and video of one source are **linked**: selecting one
selects both, and a trim applies to both. ReelForge has no such relation, so a
ripple on a V1 clip leaves its A1 counterpart at the old length and the pair
drifts apart by exactly the amount trimmed.

Sync lock (ADR 010) shifts downstream material and never trims. It keeps the
music bed where the user put it; it cannot keep a clip's own audio with its own
picture. This is the other mechanism.

M4's gate is the full trim set driven by keyboard only. A keyboard trim that
desyncs the audio of the clip being trimmed is not a workflow anyone would ship,
so this blocks the gate.

## Decision 1: a link is a group id on the clip

`Clip` gains `LinkId link`, null by default. Clips sharing a non-null id are one
group. `Document::link_clips()` issues a fresh id from the same counter every
other id comes from, so ADR 005's rules apply unchanged: ids are never reused,
the counter is document state, and undo restores it.

Rejected alternatives:

- **A pointer or index to a "partner" clip.** Works for a pair and not for a
  three-member group (picture plus two audio channels), and every removal has to
  find and fix the back-reference.
- **A separate table of groups in the document.** More faithful to how a UI
  thinks about it, and one more thing to keep consistent with the clips
  themselves. A group is a property of the clips; putting it on them means an
  ordinary clip removal cannot leave a group referring to something gone.

**Consequence:** a group whose members are removed until one is left stays a
group of one. That is harmless — a trim on it behaves exactly as an unlinked
trim — and it is what keeps undo simple, because removal does not have to
rewrite anyone else's state.

## Decision 2: linking requires the members to be aligned

`link_clips()` refuses unless every member has the same `start` and the same
`duration`, and no two members are on the same track.

This is the decision that makes everything else fall out. With aligned members, a
trim is *the same operation with the same delta on each member*, each member's
track ripples from the same point, and the reachable range is simply the
intersection of what each member allows. Nothing needs a per-member offset.

Premiere is more permissive at link time: it lets you unlink, trim one side,
relink, and then shows a red out-of-sync indicator with the offset in frames.
Requiring alignment to *create* a link is the smaller honest move now.

**A first draft of this ADR went further and claimed drift was "unrepresentable
by construction". That was wrong, and the fuzz caught it.** A ripple elsewhere
shifts downstream material on sync-locked tracks; if a link has its picture
downstream on the rippled track and its audio on a track whose sync lock the user
turned *off*, the picture moves and the audio does not. The pair is then
misaligned, and nothing should stop it — the user explicitly asked that track to
stay put, and Premiere behaves the same way, which is precisely why it has an
out-of-sync indicator to show for it.

So the invariant is narrower than it first looked, and it is the one that
actually matters for D16:

> A trim applies **the same deltas to every member**. It never introduces drift.
> It does not undo drift that an earlier edit legitimately created.

A misaligned group keeps its offset through subsequent trims, because every
member moves by the same amount from wherever it is. Surfacing that offset to the
user is a UI feature and is recorded as follow-up; without it, a desynced pair is
silent, which is a real gap and is filed rather than glossed.

**Different sources are fine, and expected.** A linked pair is usually two
streams of one file, and their `source_duration` need not match. That is handled
by the range being an intersection: if the audio has 20 ticks of tail and the
video has 200, the ripple stops at 20 and both stay together.

## Decision 3: a trim always applies to the whole group

`make_trim()` operates on every member. There is no per-call opt-out.

Premiere has a **Linked Selection** toggle that turns linking off for selection,
and it is a UI-level control: it decides what the user's click selected, not what
a trim means. When the command map arrives it can bind a variant that trims one
clip; inventing that API now, with no caller, would be guessing at its shape.
Until then the way to trim one side is to unlink, which is what Premiere users do
anyway.

## Decision 4: a track holding a member ripples as a member, not as a bystander

ADR 010 shifts every sync-locked track's downstream material. A track that also
holds a linked member must not get both: it ripples as part of the operation and
would otherwise move twice, by the trim and again by the shift.

Adobe states the rule directly — a track containing a clip that is part of the
operation shifts regardless of its sync-lock state. Here that means member tracks
are excluded from the sync-lock sweep, because the trim already moved them.

Read 2026-08-06:
<https://helpx.adobe.com/premiere/desktop/edit-projects/change-clip-sequence/sync-lock-to-prevent-changes.html>

## Consequences

- The project format goes to version 4 for `Clip::link`.
- A roll on a linked pair needs a butt-joined clip after **every** member, since
  the range is an intersection. That is the correct answer — a roll that moved
  the picture's edit point and not the audio's would desync the pair — but it
  means a roll can be refused for a reason on a track the user was not looking
  at, so the error names the member that has no neighbour.
- Bystander tracks are rippled from the **named** clip's out point, not from an
  arbitrary member's. With aligned members the two are identical; with a group
  that an earlier edit desynced they are not, and the clip the user acted on is
  the predictable choice.
- **A desynced link is invisible.** Nothing warns the user, which is the gap
  Premiere fills with its out-of-sync indicator. Filed as follow-up.
