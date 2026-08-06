# ADR 008 — Presentation, and the first thing CI cannot check

- **Status:** Accepted
- **Date:** 2026-08-05
- **Milestone:** M3

## Context

M3's gate ends with *Program monitor playback*. Everything up to here has been
verifiable mechanically on two operating systems: compositing is checked against
a CPU reference, pacing against an injected clock, decode against a linear-decode
oracle. Presentation breaks that streak. A CI runner has no display, and the one
thing presentation does — put pixels in front of a person — has no automated
oracle at all.

That is worth stating plainly rather than discovering later, because the
temptation is to let "it renders" stand in for "it renders correctly", and the
whole project has been built on refusing that substitution.

## Decision 1: the surface comes from Qt, not from platform code

`QVulkanInstance::setVkInstance()` adopts an existing `VkInstance`, and
`QVulkanInstance::surfaceForWindow()` then produces a `VkSurfaceKHR` for a
`QWindow`. ReelForge keeps ownership of the instance and the device; Qt supplies
only the surface.

Rejected: creating the surface directly with `VK_KHR_win32_surface`,
`VK_KHR_xcb_surface`, `VK_KHR_wayland_surface` and so on. That is a per-platform
code path, per-platform bugs, and a per-platform window-system dependency in
`rf_gpu` — for a job Qt already does correctly on every platform it supports.
Qt is already a hard dependency (ADR 003); using it for the part it is good at
is not a new cost.

Rejected: `QVulkanWindow`. It owns the instance, the device, the swapchain and
the render loop. ReelForge already owns all four, and the pacing decisions in
ADR 006 are not ones a widget framework should be making.

## Decision 2: instance extensions are discovered, not assumed

Surface extensions are enabled by enumerating what the loader offers and
requesting the ones present, rather than hardcoding a name per platform. A
headless build, a machine with no display, and a CI runner all then create an
instance successfully and simply have no surface extensions — which is the
correct outcome, not an error.

Presentation support is therefore a runtime capability the editor can report,
exactly as a missing driver is (ADR 007). The headless paths — export, golden
frames, `rf_playback_bench` — must keep working on a machine that cannot present
at all.

## Decision 3: the compositor does not know about the swapchain

The compositor writes into a `Texture`. Presentation copies that texture into
whichever swapchain image was acquired. The two are separate objects with no
knowledge of each other.

This keeps the M5 export path and the M3 playback path identical up to the last
step, which is what stops "what you see" and "what you export" from diverging —
a class of bug that is miserable to diagnose because both halves look correct in
isolation.

The copy costs a full-frame blit that a direct render-to-swapchain would avoid.
Accepted deliberately: iteration 6 measured compositing at 0.23 ms against a
33.33 ms budget, so there is room, and the layering is worth more than the
microseconds. Revisit if measurement says otherwise — not before.

## Decision 4: pacing moves onto the presentation engine

Headless, the pacer waits on a clock (ADR 006). With a swapchain it should wait
on `vkAcquireNextImageKHR` with FIFO present mode, which blocks until the
display is ready. That is more accurate than a sleep and lets the driver
schedule the next frame.

Both paths keep the same `FrameLog` accounting, so a drop means the same thing
whether or not anything is on screen.

## What this means for verification

**CI cannot test presentation, and never will.** What CI still covers:

- The compositor's output, against a CPU reference, pixel for pixel.
- That a machine with no surface extensions still creates an instance, still
  composites, and reports presentation as unavailable rather than failing.
- That the presentation code compiles and links on both platforms.

What is left to a human: that the frames actually appear, in order, on a
monitor. `PROGRESS.md` records the gate as met only when that has been observed,
and says who observed it — not because a machine printed a number.
