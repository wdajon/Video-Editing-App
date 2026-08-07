# ADR 018 — What is on screen at frame N, and the first picture

- **Status:** Accepted
- **Date:** 2026-08-06
- **Milestone:** M4 (toward D25)

## Context

D25: JKL moved a playhead and no picture. The Program monitor could play a file
(`--play`, M3) but had no idea what a *timeline* looked like, because nothing in
ReelForge could answer the question every part of an editor asks — **given a
playhead, what is visible, in what order, and which frame of each source?**

Playback needs it. The Program monitor needs it. M5's export needs it. Three
places that must never disagree about what an edit looks like.

## Decision 1: the question is document logic, answered once

`timeline::layers_at(document, frame)` returns the visible clips bottom-first,
each with the source frame it shows. No decoder, no GPU, no window — so the
arithmetic that decides *which picture appears* is testable on its own, and it
is the part an off-by-one ruins invisibly.

Four rules it settles:

- **Half-open coverage.** A clip ending at tick T does not cover T, so
  butt-joined clips show exactly one picture at the join rather than two or a
  one-frame hole.
- **Bottom first**, because that is compositing order, and `Document::tracks()`
  already holds them that way — so the two cannot disagree.
- **Empty is an answer, not an error.** A gap shows nothing. A renderer must
  draw black there rather than hold the last frame, or a stale picture becomes
  indistinguishable from a live one.
- **A disabled clip leaves a hole**, it does not reveal what is behind it on its
  own track. Clips on a track never overlap, so "skip and keep looking" — the
  natural way to write that loop — is wrong.

## Decision 2: rendering is its own module, not the monitor's private business

`rf_render` joins the model, the decoder and the compositor, and links no Qt.
M5's export will render the same way, one frame at a time, into an encoder
instead of onto a screen. Building it once is what stops an exported file and a
previewed frame from disagreeing.

Decoders are kept open between frames. Reopening a file per frame would make
scrubbing unusable and would surface as nothing but slowness, so
`open_sources()` is public and a test asserts that two clips of one file share
one decoder.

**Scaling is refused, not performed.** A source that is not the sequence size
fails with both sizes in the message. Stretching silently would hide a
mismatched import, and placement and scale are transform work that belongs with
the keyframe system (M6).

**A missing frame is reported, not drawn black.** A clip claiming media the file
does not have is a broken project, and drawing it as a gap would make it look
intentional — the same lie as a placeholder frame, told later.

## Decision 3: the demo timeline was resized to fit real media

It described four six-second clips. The fixture the repository ships is **two
seconds**. Pointed at real media, the demo would have claimed frames the file
never had — precisely the error Decision 2 refuses to paper over.

Clips are now twenty frames from a sixty-frame source: a third used, a third of
handle either side, which is exactly what the fixture holds.

## Decision 4: the verification is the video's own frame counter

`--render-frame N --out file.png` renders through the whole real path and writes
the result.

`testsrc2` burns its frame number into the picture, which makes it an
**independent oracle for the source-frame arithmetic**. Clip 1 begins twenty
frames into its source, so timeline frame 5 must show source frame 25 — and the
rendered image reads `25`. Timeline frame 30 falls in clip 2 and reads `30`.
Both confirmed by eye against a number the decoder did not compute.

That is stronger than any assertion in the suite, because the oracle comes from
outside ReelForge. It is recorded here rather than automated: reading the digits
back would need OCR, and a golden-image comparison needs a stable decoder across
platforms first (D23).

## Consequences

- **D25 is not closed.** There is still no *live* monitor in the window: this
  renders a frame on demand, it does not present at the shuttle rate. Embedding
  the `QWindow` and driving it from the transport is the remaining step, and it
  is now a small one — the hard part was never presentation, it was knowing what
  to present.
- The sequence size is a `--render-frame` constant taken from the fixture.
  A project carrying its own frame size is M5's work; a number in a source file
  would be a spec value in the wrong place.
- `rf_render` needs a Vulkan device, so its tests skip where there is none.
  CI's Linux runners have lavapipe and will run them.
