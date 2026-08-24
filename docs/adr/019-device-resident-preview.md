# ADR 019 — Keeping the preview on the device, and finding the real cost

- **Status:** Accepted
- **Date:** 2026-08-06
- **Milestone:** M4 (toward D30)

## Context

The Program panel showed a picture but could not play one. I told the project
owner the cause was the readback — pixels crossing PCIe for every frame — which
was a guess dressed as a diagnosis. The project's own rule is that *every*
performance guess made here has been wrong by at least 4x in one direction or the
other, so this time it was measured first.

`rf_render_bench` renders consecutive frames of a timeline and reports the
per-frame cost, plus **how many frames the decoders actually materialised**. The
counter is the important half, for the reason M1 recorded: it is deterministic
and it answers a question a stopwatch only hints at.

## What the measurement said

On the reference machine (RTX 3070), 1080x1920, one layer, 120 frames:

```
path:    readback (CPU pixels out)
decoded  120 frames for 120 rendered (1.0 per frame, floor 1)
frame ms p50 66.20  p99 80.94  max 84.91  (budget 33.33)
```

Two findings, and **my second hypothesis was also wrong**:

1. `1.0 decoded frames per rendered frame` — exactly the floor. I had guessed
   that `render()` seeking for every frame meant re-decoding from a keyframe each
   time. It does not: M1's decoder already recognises the sequential case. The
   counter settled in one run what timing alone would have left ambiguous.
2. So the cost really was the transfers, as originally suspected but not shown.

## Decision: the device-resident path is primary, readback is built on it

`render_to_texture()` composites into a texture the renderer owns and returns it.
Nothing crosses PCIe outbound. `render()` calls it and reads the result back, so
the two cannot disagree about what a frame looks like — the arrangement M3
settled on for `composite_into` and `composite`, and for the same reason.

Layer textures are created once and reused. A device allocation per frame would
sit on the playback path, which the mission's budget forbids outright, and a test
asserts the same target comes back on every call.

Measured after, same scene:

```
path:    device-resident
frame ms p50 29.76  p99 43.53  max 46.22  (budget 33.33)
```

**A 2.2x improvement, and not yet enough.** p50 is inside the budget; p99 is 30%
over. Reported as a partial result rather than a win, because it is one.

## What still cost, and what it actually was

Roughly 30 ms per frame remained, and I attributed it to decode plus the CPU
swscale pass plus the upload — reasonably, and **wrongly**. Timing the stages
separately gave decode 0.70 ms, swscale 1.42 ms, copy-upload-composite 2.02 ms.
Four milliseconds out of thirty.

The missing twenty-five was `seek_to_frame`, at **p50 23.5 ms**, called once per
layer per frame because the renderer is asked for a frame *index* rather than
"the next one". A seek is frame-accurate — it lands on the keyframe at or before
the target and decodes forward (M1) — which is exactly why it is expensive.

**Two counters failed to show this, and the reason matters.**
`frames_materialised` cannot: M1 optimised the decoder to stop copying frames a
seek discards, so a seek decoding a hundred frames still materialises one.
`frames_decoded` counts frames handed out, so it read 1.0 per frame too. Both
count real things; neither counts *this* thing. A counter is only as good as the
question it is asked, and I asked two of them a question they were not built to
answer before timing the stages directly.

## Decision 2: seek only when not already in position, and key decoders by clip

The renderer records where each decoder is standing and skips the seek when the
frame wanted is the one that comes next. Playing forward then costs one seek at
the start and none after.

Decoders are keyed by **clip**, not by file. Two clips of one file stacked on
different tracks are both visible at once at different source frames, so a shared
decoder seeks between them every frame. Keyed by clip, both play forward. The
cache is capped at eight with least-recently-used eviction, because keying by
clip would otherwise accumulate one decoder per clip a timeline had ever shown.

`seeks()` is public and a test asserts that sixteen consecutive frames perform no
seek after the first — the counter that would have caught this, counting the
thing itself rather than a proxy.

Measured on the reference machine, 1080x1920, 120 frames:

| scene | before | after | budget |
|---|---|---|---|
| 1 layer | p50 29.20 / p99 44.02 | **p50 4.67 / p99 5.19** | 33.33 |
| 3 layers | p50 64.38 / p99 99.12 | **p50 13.86 / p99 14.60** | 33.33 |

Three layers at 1080x1920 is M3's own gate workload, now rendered from a real
timeline with 2.4x of headroom.

**The shader conversion is no longer the bottleneck it was filed as.** swscale
costs 1.42 ms of a 4.67 ms frame. Doing it on the GPU is still worth having — it
also halves the upload — but it is an optimisation now, not a blocker, and D33
was re-scoped to say so.


## Consequences

- The bench stays. Every performance claim about the preview now has a command
  behind it, and the decoded-frames counter will catch a future change that
  reintroduces a seek per frame without anyone having to notice the slowdown.
- The Program panel still reads back, because a `QWidget` cannot paint a Vulkan
  texture. At 1080x1920 that readback costs roughly 36 ms on top of the 4.67 ms
  render, so the panel is still not a playback surface. Presenting through the
  swapchain is the rest of D30, and the engine behind it is now fast enough to
  make that worth doing.
- **The lesson is about counters, not about seeking.** Two existing counters both
  reported the healthy-looking `1.0 per frame` while 80% of the time went
  somewhere neither of them watched. Timing the stages separately took one run
  and settled it. Reach for the stopwatch to find *where*, then add a counter for
  *that*, rather than trusting a counter built for a different question.
