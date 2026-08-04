# ADR 001 — Toolchain and dependency management

- **Status:** Accepted
- **Date:** 2026-08-04
- **Milestone:** M0

## Context

ReelForge must build reproducibly from a clean clone on Windows and Linux, with
C++20, FFmpeg, Qt 6, Vulkan, OpenColorIO, OpenTimelineIO and a test framework.
The mission brief fixes most of the stack; what it leaves open is how the
dependencies are acquired and pinned, and that choice is hard to reverse once
build scripts, CI, and developer machines depend on it.

## Options

1. **vcpkg manifest mode.** Per-project `vcpkg.json`, versions pinned by a
   `builtin-baseline` commit plus explicit `overrides`. First-class CMake
   toolchain integration. Ships inside Visual Studio Build Tools on Windows.
2. **Conan 2.** Richer binary-package model and better prebuilt-binary reuse,
   but adds a Python runtime to the bootstrap path and a second configuration
   language (profiles) alongside CMake presets.
3. **System packages / `FetchContent`.** No pinning worth the name; a clean
   clone builds differently on two machines. Rejected on the reproducibility
   requirement alone.

## Decision

**vcpkg in manifest mode**, with `builtin-baseline` pinned to a specific vcpkg
commit and every direct dependency additionally pinned in `overrides`. The
baseline alone is not enough: it fixes the *default* version, but an override
makes the intended version explicit in the diff when it changes.

Qt 6 is the expected exception. Building Qt from source through vcpkg costs
hours per clean machine; the intent is to consume official Qt binaries and
record that as a separate ADR when Qt lands (M0 iteration 2). This ADR does not
pre-approve that; it only flags that the exception is coming.

Toolchain discovery on Windows goes through `vswhere` (`scripts/dev_env.ps1`),
never a hardcoded Visual Studio path, so the same script works against Build
Tools and full VS installs and survives VS updates.

## Consequences

- A clean clone needs exactly: a C++20 compiler, CMake ≥ 3.24, Ninja, and a
  bootstrapped vcpkg checkout pointed at by `VCPKG_ROOT`.
- Dependency upgrades are a reviewable diff in `vcpkg.json`, not an ambient
  change on someone's machine.
- vcpkg builds most ports from source on first use. CI must have a binary cache
  or clean builds will be slow. Not yet configured — tracked in `docs/BACKLOG.md`.
- Windows uses the `x64-windows-static-md` triplet: static libraries against the
  dynamic CRT. This avoids shipping a directory of third-party DLLs next to the
  editor while keeping a single shared CRT, which matters because Qt plugins and
  any future third-party effect plugins (M11) must share one CRT heap.
