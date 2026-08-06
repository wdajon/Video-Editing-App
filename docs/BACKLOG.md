# Backlog

Ideas and defects that are out of scope for the milestone in flight. Nothing
here is worked on until its milestone comes up. Adding to this file is how scope
creep gets refused without losing the idea.

## Defects (known, unfixed)

| ID | Raised | Description | Blocks |
|---|---|---|---|
| D1 | M0 i1 | `version.hpp` is generated at **configure** time, so the embedded git revision goes stale after a commit until CMake re-runs. `rf_version` can therefore report a revision the binary was not built from — which undermines the whole point of the field. Fix: regenerate through a build-time custom command that reads `.git/HEAD`. | M12 (crash reports must name the exact build) |
| D2 | M0 i1 | Nothing mechanically enforces "no exceptions across module boundaries" (ADR 002). It is review-only today. Fix: clang-tidy with a `-fno-exceptions` translation-unit check, or a link-time audit. | M11 (plugin ABI) |
| D3 | M0 i1 | CI is written but **never executed** — no git remote exists, so the M0 exit gate ("CI green on 2 OSes") is unverified. Nothing has ever been compiled on Linux; `-Wconversion -Werror` under GCC/Clang will almost certainly surface warnings MSVC does not. | M0 exit gate |
| D4 | M0 i2 | Qt is pinned to 6.10.3 because `aqtinstall` 3.3.0 (newest on PyPI) cannot resolve any Qt 6.11+ release — see ADR 003 for the measured version boundary. This holds ReelForge one LTS line behind 6.12. Recheck when `aqtinstall` publishes a release that resolves 6.11+, then move the pin in `scripts/install_qt.ps1` **and** `.github/workflows/ci.yml`. | — |
| D6 | M0 i3 | CI depends on the third-party action `ilammy/msvc-dev-cmd@v1`, last released 2023-12-31 and referenced by a mutable tag rather than a commit SHA. A mutable tag on a third-party action is a supply-chain hole. Fix: pin to a SHA, or replace it with the `vswhere` + `vcvars64` logic ReelForge already has in `scripts/dev_env.ps1`, exported to `$GITHUB_ENV`. | — |
| D5 | M0 i2 | The Qt version pin exists in two files (`scripts/install_qt.ps1`, `.github/workflows/ci.yml`) with nothing enforcing that they agree. A drift between them means CI tests a different Qt than developers do. Fix: single source of truth read by both. | — |

| D7 | M1 i2 | libav writes its own diagnostics straight to stderr. `rf_media` currently raises the threshold to errors with `av_log_set_level`, which is process-wide state owned by a third-party library and silently discards warnings that would help diagnose bad media. Fix: install an `av_log_set_callback` that routes into ReelForge's logger with the stream and file as context. Referenced from `src/media/libav_error.hpp`. | M12 (crash/diagnostic reporting) |
| D8 | M1 i3 | `VideoFrame` copies every decoded frame out of libav into an owned, tightly packed buffer. That is correct, portable, and what makes `frame_hash` reproducible — but a full-frame copy per frame will not fit the M3 playback budget at 1080x1920. Fix: a zero-copy path that hands `AVFrame` buffers (or hardware frames) to the GPU uploader, keeping the packed copy only for hashing and tests. Referenced from `include/rf/media/video_frame.hpp`. | M3 (playback budget) |

| D9 | M1 i4 | **Random seek into long-GOP 4K misses the 150 ms budget.** Measured on the reference machine at p99 **355 ms** (mean 194 ms) against a 10-minute 3840x2160 GOP-250 source — see the baseline table in `docs/PROGRESS.md`. Accuracy is unaffected and passes. Two fixes already landed (decoder threading, and not copying frames a seek discards) took it from 2534 ms to 355 ms; the remaining gap is not reachable by tuning this loop, because an average seek must decode ~125 frames of 4K on the CPU. Fix: hardware decode (D3D11VA / NVDEC / VideoToolbox) and proxy media. **M3 must not be declared complete while this is unmet** — M3's own gate measures sustained playback, which can pass with slow seeks, so this needs checking explicitly. | M3 |

| D10 | M2 i1 | **TSan does not cover libav's internal threading.** vcpkg builds FFmpeg without sanitizer instrumentation, so ThreadSanitizer cannot observe libav's own happens-before edges and reports its frame threading as data races — every frame landing in `pthread_frame.c`, `frame.c` or `h2645_parse.c`. Decoder threading is therefore forced to one thread under TSan (`RF_THREAD_SANITIZER`), which means **the TSan job validates ReelForge's concurrency but not the multi-threaded decode path that ships**. Suppressions were rejected: they would also hide real races surfacing through a libav call. Fix: build FFmpeg with `-fsanitize=thread` via a custom vcpkg triplet for the TSan job only. | M3 (job system — the first ReelForge code that is genuinely concurrent) |

| D11 | M3 i2 | **The OpenGL 4.3 fallback required by the brief does not exist.** Only the Vulkan path is built. Deferred deliberately (ADR 007): an abstraction cannot be designed honestly from a single implementation, and inventing a seam now risks one that fits neither backend. The constraint accepted in exchange is enforced by the build — Vulkan include directories are PRIVATE to `rf_gpu` — so the extraction stays confined to one module. | M3 exit (the brief names it as a stack constraint) |
| D12 | M3 i2 | The Vulkan loader leaks one fixed-size allocation of process-global state, freed only on library unload, and volk never `dlclose`s it. Suppressed narrowly for `rf_gpu_tests` via `tests/lsan-suppressions.txt` rather than by disabling leak detection, because `rf_gpu` is where leak detection matters most — every Vulkan object there is freed by hand. `print_suppressions=1` is on so the CI log shows whether the suppression is still matching anything; if it stops matching, the suppression is stale and should be deleted. | — |

| D13 | M3 i5 | **RESOLVED (M3 i6).** The composite API took CPU pixels and returned CPU pixels synchronously -- about 33 MB across PCIe per frame -- measured at p50 49.9 ms against a 33.3 ms budget. `Compositor::composite_into()` now takes GPU-resident `Texture` layers and writes a GPU `Texture`, with no upload and no readback in the loop: **p50 0.23 ms, p99 0.42 ms, zero drops**, a 217x improvement that confirms the cost was transfers rather than blending. `composite()` remains as the export and golden-frame path and is now implemented on top of `composite_into()`, so the two cannot disagree. Remaining caveat: the bench composites as fast as it can rather than pacing, so its zero-drop result measures capacity, not paced playback. | resolved |

| D14 | M4 i1 | `Clip::source_duration` records the length of a clip's media on the clip itself, so two clips cut from one source repeat the value and nothing enforces that they agree. It is where the field has to live today, because `Clip::source` is a path string and there is no media pool. Fix: a media pool keyed by source, holding the available range, with the clip's span validated against it — the shape OpenTimelineIO uses (`available_range` on the media reference). The invariant moves intact; see ADR 009. | M5 (import needs a media pool anyway) |
| D15 | M4 i1 | A ripple trims one track only. Premiere ripples every sync-locked track together, so a keyboard ripple on V1 leaves the paired A1 clip behind and the edit goes out of sync. Fix: track sync-lock state, and a ripple that rewrites every locked track in one command — `Document::replace_track_clips` is per-track, so this needs either a compound command or a multi-track primitive. | M4 exit (the gate is a keyboard-only trim workflow, and a desynced edit is not one) |

## Deferred work

- **vcpkg binary caching in CI** (ADR 001). Clean builds will get slow once
  FFmpeg and Qt are in the manifest.
- **`RF_TRY` macro** for early-return propagation. Needs statement-expressions
  (GCC/Clang) or C++23 to be portable; revisit rather than ship a macro that
  works on two of three compilers.
- **`Result` in a `constexpr` context** is only partially exercised by tests.
- **macOS support.** The brief says cross-platform and gates on "2 OSes";
  Windows + Linux is the current pair. macOS adds Metal (vs Vulkan) and a
  different Qt deployment story.
- **4:5 feed and 1:1 preset research** — see the open questions in `docs/SPECS.md`.
  M5 and M7 are blocked on it.
