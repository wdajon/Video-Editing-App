# Handoff — resuming ReelForge in a new session

Read this first, then `docs/PROGRESS.md` (state), `docs/BACKLOG.md` (open
defects), and `docs/adr/` (why things are the way they are). The mission and the
working protocol are in `docs/MISSION.md`.

Repository: https://github.com/wdajon/Video-Editing-App (public)

---

## Where the work stands

| Milestone | State |
|---|---|
| M0 — repo, CMake, deps, CI, Qt shell | **Gate met.** Six-job CI matrix green. |
| M1 — probe, decode, frame-accurate seek | **Gate met.** 200/200 random seeks correct on a 10-min 4K file. Performance budget **not** met — see D9. |
| M2 — timeline model + undo/redo | **Gate met.** 10,000-operation fuzz, undo returns byte-identical. |
| M3 — GPU compositor + playback | **In progress, 3 iterations.** Gate not met. |
| M4 onward | Not started. |

249 tests. Zero warnings at `/W4 /WX` and `-Wall -Wextra -Werror`.

### What M3 still needs

1. Composite multiple layers with alpha, against golden frames.
2. Present to the Program monitor (swapchain) — first code needing a window.
3. Measure sustained playback on the reference machine and record it.
4. **Hardware-accelerated decode.** Now on the critical path: CPU decode manages
   ~66 fps on 4K, and three layers at 30 fps needs ~90 decoded frames per second
   before any compositing. Also what D9 needs.

---

## This machine

Nothing here is in the repo, and a new session will not discover it by itself.

| Thing | Where |
|---|---|
| Repo | `A:\Development\Claude\ReelForge` |
| vcpkg | `A:\vcpkg` (pass `-VcpkgRoot A:\vcpkg`) |
| Qt 6.10.3 | `A:\Qt\6.10.3\msvc2022_64` (set `QT_ROOT`) |
| Toolchain | VS 2022 Build Tools, MSVC 19.44, bundled CMake 3.31.6 + Ninja 1.12.1 |
| GPU | RTX 3070, Vulkan 1.4.341. **Reference machine for all perf numbers.** |
| 10-min 4K test source | `A:\rf-large-media\` (2.9 GB, not in git — regenerate per `tests/fixtures/media/README.md`) |
| `gh` CLI | Installed and authenticated as `wdajon`. Used to read CI results. |

### Building

```powershell
. .\scripts\dev_env.ps1 -VcpkgRoot A:\vcpkg
$env:QT_ROOT = 'A:\Qt\6.10.3\msvc2022_64'
cmake --preset windows-debug
cmake --build build/windows-debug --parallel
ctest --preset windows-debug
```

`dev_env.ps1` prints a harmless `vswhere.exe is not recognized` line that comes
from inside `vcvars64.bat`. It is not an error.

**Historical note:** the repo used to live at `A:\Development\Claude\Video
Editing app`. FFmpeg's MSYS build passes the vcpkg library path to `link.exe`
unquoted, so a space in the path split the argument and the link failed with
`LNK1181: cannot open input file 'Editing.obj'`. The workaround was
`-DVCPKG_INSTALLED_DIR=<space-free path>`; renaming the directory removed the
need for it. A guard in `CMakeLists.txt` still catches the situation before
`project()` if it ever recurs.

---

## Hardware-dependent checks CI cannot run

Both are real gates that a green CI does not cover. Run them on the reference
machine and record the numbers in `docs/PROGRESS.md`.

```powershell
# Seek accuracy and latency on 4K. Currently: 0 mismatches, p99 355 ms vs a 150 ms budget (D9).
.\build\windows-release\bin\rf_seek_check.exe A:\rf-large-media\testsrc2_3840x2160_30fps_600s.mp4 --seeks 200

# Which GPU ReelForge would use.
.\build\windows-debug\bin\rf_gpu_info.exe
```

CI's Linux jobs run Vulkan against Mesa's **lavapipe** — a real software Vulkan
implementation, ~100x slower than hardware. It proves correctness and can never
prove a frame-rate gate.

---

## Decisions the project owner has made

- **Licence: GPL-3.0-or-later**, confirmed. `LICENSE` carries the full text.
  This is what allows x264 at M5.
- **Directory renamed** to remove spaces.

## Open defects

Full detail in `docs/BACKLOG.md`. The ones that shape upcoming work:

- **D9** — random 4K seek is p99 355 ms against a 150 ms budget. Fixed 5.8x
  already (threading, and not copying discarded frames); the rest needs hardware
  decode. **M3 must not be called done while this is unmet**, because M3's own
  gate measures sustained playback, which can pass with slow seeks.
- **D11** — the OpenGL 4.3 fallback the brief requires does not exist. Deferred
  deliberately (ADR 007); Vulkan includes are PRIVATE to `rf_gpu` so the
  extraction stays confined to one module.
- **D8** — every decoded frame is copied out of libav. Correct and portable, and
  too slow for the M3 playback budget. Needs a zero-copy path to the GPU.
- **D10** — TSan cannot see libav's internal threading, so decoder threading is
  forced to one thread under TSan. The shipping multi-threaded decode path is
  therefore not TSan-covered.

---

## Conventions worth keeping

These were arrived at the hard way and are visible throughout the codebase.

- **Never claim a gate without pasted command output.** Several times the
  measured result contradicted a confident prediction.
- **Verify on a clean tree.** A Qt deployment race passed locally for days
  because the build directory was already populated; only a clean build
  exercised it.
- **A failure that looks deterministic is not, until it fails twice.** An
  aqtinstall extraction failure looked like a path bug — with a real path bug
  visible in the log — and was actually flaky.
- **Prefer a counter to a stopwatch for performance regressions.** `SeekCost`
  asserts a seek copies exactly one frame; it is deterministic and immune to CI
  timing noise, and it was verified by reintroducing the bug and watching all
  ten correctness tests still pass while it alone caught the regression.
- **Fix the API, not the test.** A `VkDevice` outliving its `VkInstance` crashed
  a test; ownership changed so it cannot happen, rather than the test being
  corrected.
- **State what a test does not prove.** Fixture READMEs and ADRs say plainly
  where coverage stops — synthetic media has no VFR or broken timestamps,
  lavapipe says nothing about frame rate.
