# ADR 007 — GPU backend, and how it is tested without a GPU

- **Status:** Accepted
- **Date:** 2026-08-04
- **Milestone:** M3

## Context

The brief fixes the API: Vulkan primary, OpenGL 4.3 fallback, every effect a
shader. What it leaves open is how ReelForge links to Vulkan, how shaders get
compiled, and — the part that actually constrains the design — **how any of it
is verified on a CI runner with no GPU**.

That last point is not a detail. Every milestone so far has been held to
mechanical verification on two operating systems. A GPU layer that can only be
tested by a human looking at a window would be the first part of the project to
escape that, and it would be the part where "looks right" is least trustworthy.

## Decision 1: link Vulkan dynamically through volk, not the SDK

`volk` loads `vulkan-1.dll` / `libvulkan.so.1` at runtime and resolves entry
points itself. Consequences:

- **No Vulkan SDK install.** vcpkg supplies `vulkan-headers` and `volk`; the
  loader is whatever the machine already has. `vcpkg`'s own `vulkan` port
  requires a `VULKAN_SDK` environment variable, which would make a clean clone
  fail on a machine without a manual SDK install — exactly the kind of hidden
  prerequisite ADR 001 exists to prevent.
- **A machine with no Vulkan is a runtime condition, not a link error.** The
  editor must start on a machine with a broken or absent driver and say so,
  rather than failing to launch. That is only possible with dynamic loading.
- Device-level entry points are loaded per device, which avoids the dispatch
  overhead of going through the loader's trampoline on every call.

## Decision 2: rendering is headless by default; the swapchain is a presenter

The render graph produces images. Putting one on screen is a separate concern
handled by a presenter that owns the surface and swapchain.

This is a layering decision, and it is what makes the GPU work testable:

- Golden-frame tests render to an offscreen image and read it back. No window,
  no surface, no compositor, no display server.
- The headless path is also what `rf_render_headless` (M5) needs to export
  without a GUI, so it is not test-only scaffolding — it is the primary path,
  and the on-screen case is the special one.

## Decision 3: correctness in CI on a software device, performance only on real hardware

CI runners have no GPU. Rather than exclude the GPU layer from CI:

- **Linux CI installs Mesa's lavapipe**, a software Vulkan implementation. It is
  a real Vulkan driver: API misuse, validation-layer errors, synchronisation
  mistakes and wrong pixels are all caught. It is roughly two orders of
  magnitude slower than hardware.
- **Performance is measured locally**, on the reference machine, exactly as the
  4K seek check is (see D9). A frame-time budget measured on lavapipe would be
  meaningless.

So the split is explicit and written down: **CI proves the GPU code is correct;
it does not and cannot prove the M3 frame-rate gate.** The gate number comes
from the reference machine and is recorded with its hardware, as the seek
baseline is.

Tests skip rather than fail when no Vulkan device can be created at all, because
a developer without a driver should still be able to run the suite. A skip is
visible in CTest output; a silently passing test is not.

## Decision 4: validation layers on in debug, off in release

Vulkan's validation layers catch the entire class of "works on my driver"
defects. They are enabled in debug and sanitizer builds when present, and their
messages are routed into ReelForge's error handling rather than to stdout. A
validation error in a test run must fail the test, not scroll past.

If the layers are unavailable, that is not an error — they ship with the SDK,
which Decision 1 does not require.

## Decision 5: the OpenGL fallback is deferred, deliberately

The brief requires an OpenGL 4.3 fallback. It is not built yet, and this ADR
records why rather than letting it quietly not happen: the abstraction cannot be
designed honestly from one implementation. Building the Vulkan path first and
extracting the interface when the second backend arrives produces a better seam
than inventing one now and discovering it fits neither.

The constraint accepted in exchange: **nothing above `rf_gpu` may include a
Vulkan header**, so the eventual extraction is confined to one module. That is
enforced by the build, as with libav in ADR 004 — Vulkan include directories are
PRIVATE to `rf_gpu`.

Tracked as D11.

## Consequences

- `vcpkg.json` gains `vulkan-headers` and `volk`. Shader compilation tooling
  (`glslang`) arrives with the first shader, not before.
- `rf_gpu` owns all Vulkan types. `rf_playback`, `rf_timeline` and the UI never
  see one.
- A machine with no GPU runs every test except the GPU ones, which skip loudly.
- The M3 gate's frame-rate number is a reference-machine measurement, recorded
  with its hardware. CI green does not imply the gate is met.
