# ADR 019 — Keeping the preview on the device, and what still costs

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

## What still costs, and what would fix it

Roughly 30 ms per frame remains: decode, YUV to RGBA on the CPU through swscale,
and an 8.3 MB upload of the converted RGBA.

The known answer is the one M3 already wrote down as its third caveat: **upload
the Y, U and V planes and do the matrix in the shader.** That moves ~3.1 MB
instead of ~8.3 MB and removes the swscale pass entirely — it attacks both
remaining costs at once. It is a new shader path and a real piece of work, and it
is filed rather than half-started.

Until then the preview plays smoothly at small sizes (320x240 measures p50
2.92 ms) and stutters at delivery sizes. That is a limitation with a number
against it, not a mystery.

## Consequences

- The bench stays. Every performance claim about the preview now has a command
  behind it, and the decoded-frames counter will catch a future change that
  reintroduces a seek per frame without anyone having to notice the slowdown.
- The Program panel still reads back, because a `QWidget` cannot paint a Vulkan
  texture. Presenting through the swapchain is the rest of D30 and is what turns
  this measurement into playback on screen.
