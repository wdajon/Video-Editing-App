# ADR 006 — The playback clock, and what counts as a dropped frame

- **Status:** Accepted
- **Date:** 2026-08-04
- **Milestone:** M3

## Context

M3's exit gate is "1080x1920, 3 layers, sustained 30 fps playback, **no dropped
frames** over 60 s". That claim is only as good as the definition behind it, and
a project can pass a badly-defined version of it while stuttering visibly.

Three ways to get this wrong, all common:

- **Measuring throughput instead of pacing.** Rendering 1,800 frames in 60
  seconds averages 30 fps and can still stutter continuously. An average hides
  every drop.
- **Deriving position from a frame counter.** Advancing "one frame per rendered
  frame" cannot drop by construction, so the metric reports success no matter
  how late the renderer is. Position must come from the clock, and the renderer
  must be measured against it.
- **Floating-point time.** 1001/30000 is not representable; accumulating it
  60 seconds at a time drifts the playhead off the frame grid, which is the same
  ±1 defect M1 spent an entire milestone avoiding.

## Decision 1: position is a pure function of wall time

`PlaybackClock` holds an anchor — a wall instant and the timeline frame showing
at that instant — plus a rate. Position is computed from the elapsed time since
the anchor, never accumulated:

```
frame(now) = anchor_frame + floor((now - anchor_time) * frame_rate * rate)
```

Nothing integrates, so nothing drifts. A stalled renderer changes what the user
sees; it does not change where playback *is*. That is what makes a drop
detectable rather than absorbed.

The arithmetic reuses `rf::media::rescale`, so it is exact rational maths at
128-bit width. `floor` is the correct rounding: frame N is on screen for
`[N, N+1)` of its own duration, so the frame showing at any instant is the one
whose interval contains it.

## Decision 2: the clock is injected, never read from the environment

`Clock` is an interface. `SteadyClock` wraps `std::chrono::steady_clock` for the
real application; `ManualClock` is advanced explicitly by tests.

A playback test that sleeps is slow, flaky, and cannot exercise a 60-second
sustained run without taking 60 seconds. A test that advances a manual clock can
simulate an hour of playback in milliseconds, deterministically, and can place
a late frame at an exact instant to check it is counted as late.

`steady_clock` specifically, never `system_clock`: an NTP correction mid-render
would otherwise appear as thousands of dropped frames.

## Decision 3: a dropped frame is one the clock passed and the renderer never showed

When the renderer presents frame `N` having last presented frame `M`, during
forward playback at rate 1:

```
dropped += max(0, N - M - 1)
```

This counts frames the timeline moved through that never reached the screen. It
deliberately does **not** count:

- **A late frame that is still the right frame.** Presenting frame `M+1` 40 ms
  after `M` at 30 fps is late, not dropped. Lateness is tracked separately, as a
  distribution, because it is what the p99-frame-time budget measures.
- **Repeated presentation of the same frame while paused.** Nothing is being
  passed by.
- **Discontinuities from seeking.** A seek re-anchors the clock; the frames
  jumped over were never due.

Rate changes and reverse playback (JKL shuttle, M4) re-anchor the clock, so drop
counting is always relative to the current anchor and direction.

## Consequences

- `rf_playback` depends on `rf_core` and `rf::media` for `Rational`. It does not
  depend on the GPU, on Qt, or on a decoder, and a 60-second sustained-playback
  scenario is a unit test that runs in milliseconds.
- The gate becomes falsifiable: "0 dropped frames over 1,800 frame intervals,
  with p99 present-to-present within budget", both measured against clock time.
- The same accounting serves the M3 GPU work and the M4 JKL shuttle, so the
  definition of "dropped" is written once rather than per feature.
- Wall-clock behaviour of the real `SteadyClock` is exercised by a small number
  of tests; the bulk of the logic is tested against `ManualClock`.
