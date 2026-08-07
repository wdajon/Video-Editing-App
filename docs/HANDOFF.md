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
| M3 — GPU compositor + playback | **Gate met** 2026-08-05. 1800 frames presented, 0 dropped, p99 36.67 ms, confirmed visually by the project owner. See the caveats in `PROGRESS.md`. |
| M4 — panels, docking, workspaces, JKL | **Iteration 6. Gate met mechanically on Windows** — the full trim set driven by `QTest::keyClick` on a real docked panel, plus JKL (ADR 009–014). **Not confirmed by CI** (Actions outage) and **not seen by anyone**. JKL moves a playhead, not a picture (D25). |
| M5 onward | Not started. |

Zero warnings at `/W4 /WX` and `-Wall -Wextra -Werror`.

**Do not quote a test count from this file or from `PROGRESS.md`.** Both contain
figures that were true when written — `PROGRESS.md` in particular quotes pasted
output from each iteration, so its numbers are deliberately historical. A stale
count in this file was read as current by a fresh session and reported back as
fact. Get the number from the suite:

```powershell
ctest --preset windows-debug
```

At M4 iteration 9 (2026-08-06) that was **488**: core 48, media 92, timeline 130,
edit 63, gpu 42, playback 40, app 73. Treat it as a dated snapshot, not a claim
about now.

**Adobe's shortcut page is readable — through the browser tool, not `WebFetch`,
which times out on it.** Two sessions' worth of "the page could not be fetched"
was a tooling mistake, not a property of the source. ADR 016 has the transcribed
table; go back to the page for anything it does not cover.

**Two things are outstanding and a fresh session must not read past them.** CI
has never run against M4 iterations 4, 5 or 6 — GitHub Actions was in a major
outage — so that code has only ever been compiled by MSVC, with no Linux, Clang,
ASan, UBSan or TSan coverage. And nobody has looked at the Timeline panel; its
painting has no oracle (D23), exactly as presentation has none (ADR 008).

### Seeing the editor run

```powershell
.\build\windows-release\bin\reelforge.exe --demo-timeline
```

Four two-second clips on V1, each linked to its sound on A1, every clip with two
seconds of handle at both ends. The Timeline docks at the bottom and holds
keyboard focus from launch; the **Tools panel docks on the left and lists every
command with its shortcut on the button**, so nothing has to be memorised — click
or press, they run the same code. The status bar shows the live tool, the
selected clip and the armed edge, which is how you tell a keystroke registered
when it changes no clip.

**Mouse:** click a clip to select it, drag it to move it, drag the ruler strip at
the top to move the playhead.

**Keys, Adobe's own** (ADR 016, read from their page): `Ctrl+Alt+←/→` slips,
`Alt+,`/`Alt+.` slides, `Alt+←/→` nudges, `←`/`→` step the playhead, `Space`
plays. All act on the selected clip with **no tool needed**. Add `Shift` for five
frames. `L`, `J`, `K` shuttle — the playhead only, because nothing decodes at the
shuttle rate yet (D25).

Ripple and roll still use a tool (`B`, `N`) plus `[`/`]` and `Ctrl+←/→`, which
are **ReelForge's own keys, not Premiere's** (D26).

The flag exists because the panel's painting has no automated oracle (D23) and
neither does the feel of a keyboard trim. Both need a person. There is no project
loading yet, which is why an empty window shows nothing.

### Seeing playback run

```powershell
.\build\windows-release\bin\reelforge.exe --play "A:\rf-large-media\reels_1080x1920_30fps_60s.mp4"
```

A vertical window plays colour bars with a red tint and a white wash over them,
smooth for 60 seconds, then closes and prints frame statistics. Regenerate the
source with the command in `tests/fixtures/media/README.md` if it is missing.

**Presentation has no automated oracle** (ADR 008). CI proves the compositor's
output against a CPU reference and that a headless machine still composites, but
only a person can confirm frames reached the screen. Re-confirm visually after
any change to the swapchain or the monitor.

### What M3 delivered, and what it did not

**Compositing throughput is solved (D13, closed).** `Compositor::composite_into()`
takes GPU-resident `Texture` layers and writes a GPU `Texture` — no upload, no
readback in the loop. Measured on the RTX 3070 at **p50 0.23 ms, p99 0.42 ms,
zero drops**, against a 33.33 ms budget: a 217x improvement over the CPU-pixels
API, which had measured p50 49.90 ms with 856 frames dropped. The cost was
transfers all along, never the blending.

`Compositor::composite()` (CPU pixels in and out) is kept for export and golden
frames, and is implemented on top of `composite_into()` so the two cannot
disagree.

**Pacing and presentation are done.** `Pacer` waits until each frame is due and
renders whatever the clock says is current; skipping to the current frame rather
than the stale one is what keeps playback from drifting further behind on every
slow frame. `Swapchain` blits the composited texture into an acquired image and
presents under FIFO, so the display sets the pace.

**What M3 did NOT deliver**, carried forward:

1. **Hardware-accelerated decode (D9).** CPU decode manages ~66 fps on 4K and
   three layers at 30 fps needs 90, so 4K playback and the 150 ms seek budget
   both still fail. The single most valuable open item.
2. **The OpenGL 4.3 fallback (D11)**, which the brief names as a stack
   constraint. Deferred deliberately; Vulkan includes are PRIVATE to `rf_gpu` so
   the extraction stays confined to one module.
3. **YUV to RGBA on the GPU (D8-adjacent).** The conversion runs on the CPU per
   frame. It fits at 1080x1920 and will not at 4K.
4. **Frames in flight.** The loop submits and waits. FIFO already paces it, so
   there is headroom, but a heavier scene would want pipelining.

Run the throughput measurement with:

```powershell
.\build\windows-release\bin\rf_playback_bench.exe --width 1080 --height 1920 --layers 3 --fps 30 --seconds 60
```

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
- **D25** — JKL shuttles the playback clock and the Timeline's playhead, but
  nothing decodes or presents at the shuttle rate, so pressing L shows no video.
  The Program monitor is outside the widget tree by design (ADR 013).
- **D23** — the Timeline panel's painting has no oracle. Tests prove it does not
  crash, not that anyone can use it.
- **D20** — a 1/90000 tick base cannot express 23.976 fps, so such a project
  cannot be created. Refused rather than rounded; needs a base like flicks.
- **D18** — a desynced link is invisible. A ripple upstream can legitimately pull
  a linked pair apart when the user has unlocked one track's sync; Premiere shows
  a red out-of-sync indicator and ReelForge shows nothing. **Open against the M4
  gate.**
- **D14** — media length lives on `Clip::source_duration` rather than in a media
  pool, so two clips cut from one source repeat the value.
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
  lavapipe says nothing about frame rate, and the playback bench measures
  capacity rather than paced playback.
- **Measure before optimising, and again after.** Every performance guess made
  in this project was wrong by at least 4x in one direction or the other. The
  4K seek fix, the composite fix, and the threading change were all sized
  correctly only after measurement.
- **Do not edit files with shell text substitution.** PowerShell 5.1 reads UTF-8
  as ANSI, so `Get-Content -Raw` piped to `Set-Content -Encoding utf8`
  double-encodes every non-ASCII character and adds a BOM. It corrupted 92
  characters across three documents here. Worse, a `-replace` whose pattern
  contains one of those characters then silently matches nothing and reports
  success — two edits in this project were lost that way. Use editing tools that
  preserve encoding, and check the file afterwards rather than trusting an exit
  code.
