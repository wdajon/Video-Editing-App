# Handoff — resuming ReelForge in a new session

Read this first, then `docs/PROGRESS.md` (state), `docs/BACKLOG.md` (open
defects), and `docs/adr/` (why things are the way they are). The mission and the
working protocol are in `docs/MISSION.md`.

Repository: https://github.com/wdajon/Video-Editing-App (public)

---

## READ THIS FIRST: most of M4 is not on `main`

`main` is at **M4 iteration 3**. Eighteen further commits live on branches, none
merged.

| Branch | Contains | State |
|---|---|---|
| `main` | through M4 i3 (linked clips) | last merged point |
| `m4-command-map` | i4 — the command map | **PR #4**, all six jobs green 2026-08-25 |
| `m4-qt-panels` | i5–i17 — panels, workspaces, JKL, mouse, Adobe bindings, Tools strip, the render path, the Program panel, the device-resident preview, seek-free playback, the presenting surface, the route report | stacked on `m4-command-map`, **PR #5**, all six jobs green 2026-08-25 |

```powershell
git checkout m4-qt-panels   # everything described below lives here
```

**The Actions outage is over.** It began 2026-08-06 and was resolved well before
2026-08-25; the status API reports the Actions component operational. The reason
no run appeared for nineteen days is separate and worth knowing: `ci.yml` fires
only on a push to `main` and a PR targeting `main`, and PR #4 was opened at
21:04 on 2026-08-06 — mid-outage — so its `pull_request` event was dropped and
never redelivered. There is **no `workflow_dispatch`**, so CI cannot be started
from the CLI. To fire it, reopen the PR (`gh pr close N && gh pr reopen N`), push
a commit to the branch, or open another PR.

**Do not merge either branch until CI is green on the branch itself.** Check
with:

```powershell
gh run list --limit 5
```

Merge `m4-command-map` first so the stack lands in order.

### What CI found the moment it ran, and what it means for MSVC

Two errors so far, on code that green MSVC builds had passed. **They arrived one
at a time**: ninja stops at the first failure, so each fix is what lets the build
reach the next. Expect that to continue — a green Linux job is the only evidence
that there is not a third.

```
tests/timeline/trim_fuzz_test.cpp:255: error: enumeration value 'nudge'
    not handled in switch [-Werror,-Wswitch]
tests/app/timeline_panel_test.cpp:175: error: implicit conversion loses integer
    precision: 'qsizetype' to 'const int' [-Werror,-Wshorten-64-to-32]
```

Both are fixed in i17, and the first came with a real coverage gap behind it —
`nudge` had never been in the trim fuzz's `kKinds` at all.

**One of the two classes can be caught here, and one cannot. That was measured,
not assumed.**

- *Missing enum case.* MSVC leaves C4062 off even at `/W4`. `/w14062` is now in
  `cmake/ReelForgeWarnings.cmake`; deleting the `nudge` case again fails the
  MSVC build with `C4062` as an error, so the flag does what it claims.
- *Narrowing conversion.* MSVC does **not** diagnose it. `/w14244 /w14267` were
  tried alongside the `/w14242` already present, verified in
  `compile_commands.json` as reaching the file, and a plain
  `const int x = <long long>` still compiled silently under `/W4 /WX`. They were
  removed rather than kept as decoration.

So: **a green MSVC build is not evidence about narrowing.** Only the Linux jobs
are. Same for `-Wold-style-cast` and the rest of the GCC/Clang set.

### What to do first

1. **Get both PRs green and merge them in order**, `m4-command-map` then
   `m4-qt-panels`. Nothing else should merge before that.
2. **Ask the project owner to run `--demo-timeline`** and say whether the panel
   behaves. M4 cannot be called done without it, the same way M3 could not.
   D23 is why: the panel's painting has no oracle.

Do **not** start M5 until that is cleared. The milestone ladder is not advisory
(see `docs/MISSION.md`).

### The presenting path runs; nobody has watched it (D30)

The Program dock titles itself by which route carried the last frame, and the
three titles are distinct on purpose:

| Title | Meaning |
|---|---|
| **Program** | The frame was blitted into a swapchain image and presented. |
| **Program (software preview)** | Rendered, then read back off the device and painted by the widget — about 36 ms a frame more at 1080x1920. |
| **Program (no picture)** | Nothing has rendered, so no route is live. |

**This check used to be worthless and now is not** (D35). Until i17 the dock was
*constructed* titled "Program" — the exact string that means presenting — and was
corrected only inside `refresh_playhead`, which runs on a shuttle change or the
playhead timer and nothing else. Launch, scrub, trim, read the title: "Program",
meaning nothing. Underneath it, `refresh_playhead` was also the only caller of
`attach_surface`, so a session that never pressed J, K or L never asked whether
the machine could present at all.

On the reference machine the presenting branch **is** taken:

```powershell
.\build\windows-release\bin\reelforge.exe --demo-timeline --screenshot out.png
```

writes a PNG whose Program dock reads plain **"Program"**. That is the code path,
not a pair of eyes — `QWidget::grab` cannot capture a native child window, so the
panel area is black in that image. **Whether a person sees a correct picture
there is still unverified** (ADR 008) and still needs the owner.

---

## Where the work stands

| Milestone | State |
|---|---|
| M0 — repo, CMake, deps, CI, Qt shell | **Gate met.** Six-job CI matrix green. |
| M1 — probe, decode, frame-accurate seek | **Gate met.** 200/200 random seeks correct on a 10-min 4K file. Performance budget **not** met — see D9. |
| M2 — timeline model + undo/redo | **Gate met.** 10,000-operation fuzz, undo returns byte-identical. |
| M3 — GPU compositor + playback | **Gate met** 2026-08-05. 1800 frames presented, 0 dropped, p99 36.67 ms, confirmed visually by the project owner. See the caveats in `PROGRESS.md`. |
| M4 — panels, docking, workspaces, JKL | **Iteration 17, on `m4-qt-panels`. Gate met mechanically on Windows** — the full trim set driven by `QTest::keyClick` on a real panel, plus JKL, a Tools strip, mouse editing and a working render path (ADR 009–019). **Not signed off by the owner**, which is the remaining blocker. CI runs again and is **green on both branches** (PR #4, PR #5), so iterations 4–17 have now been through GCC, Clang, ASan, UBSan and TSan. The Program panel presents through the swapchain where it can and reads back where it cannot, and the presenting branch is now known to execute on the reference machine — but not to have been *seen* (D30, ADR 008). |
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

At M4 iteration 17 (2026-08-25, on `m4-qt-panels`) **529 of 529 passed** on a
clean tree, in both configurations: app 89, core 48, media 92, timeline 141,
edit 63, gpu 42, playback 40, render 14. Treat it as a dated snapshot, not a
claim about now.

**Windows Smart App Control blocked `rf_app_tests.exe` at M4 i16 and does not
now (D34).** The 89 app tests run here again, through two clean rebuilds, with
the policy unchanged — `VerifiedAndReputablePolicyState` is still 1 under
`HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy`. Nothing was done to earn
that, so nothing holds it: Smart App Control judges on reputation, which moves on
its own. Expect it back.

The symptom, if it returns: `ctest` fails at discovery with *"Error running test
executable ... Result: unknown error"*, and the binary run directly says *"An
Application Control policy has blocked this file"*. It is not a build fault —
every other suite passes, `reelforge.exe` itself runs, and a copy of the blocked
binary at another path is blocked too, so the judgement is on content rather than
location. If it happens, run the rest directly and **say which suites were
skipped**, rather than reporting a total that quietly excludes them:

```powershell
foreach ($n in @('rf_core_tests','rf_media_tests','rf_gpu_tests','rf_playback_tests','rf_timeline_tests','rf_edit_tests','rf_render_tests')) { & ".\build\windows-debug\bin\$n.exe" }
```

A separate and much milder thing that looks similar: a **first** `ctest` after a
clean rebuild timed out at discovery once here and passed immediately on retry —
Defender scanning binaries it has never seen. One retry distinguishes them.

**Adobe's shortcut page is readable — through the browser tool, not `WebFetch`,
which times out on it.** Two sessions' worth of "the page could not be fetched"
was a tooling mistake, not a property of the source. ADR 016 has the transcribed
table; go back to the page for anything it does not cover.

**One thing blocks calling M4 done, and it is not code.** The project owner has
not signed off on the panel; its painting has no oracle (D23), exactly as
presentation has none (ADR 008), so a person has to look at it — M3's gate needed
the same. The other blocker, CI, is answered: it runs again, and getting the two
PRs green and merged in order is mechanical work, not a wait.

### Seeing the editor run

```powershell
.\build\windows-release\bin\reelforge.exe --demo-timeline
```

Four twenty-frame clips on V1, each linked to its sound on A1, with twenty frames
of handle at both ends -- sized to the sixty-frame fixture the repository ships,
so the same demo also renders. The **first three are butt-joined** — that is where ripple,
roll, slip and slide work, and clip 2 is selected at launch — and the **fourth
sits past a gap**, which is where a nudge has somewhere to go. Those two wants
are opposites, so no single clip can serve both.

The Timeline is the **central widget**; the **Tools strip docks on the left** with
five tools in three slots (click to use, **click and hold** for the flyout).
Every other command is in the Clip, Sequence, Playback and Edit menus with its
shortcut beside it. The status bar shows the live tool, the selected clip, its
position and **its source range** — the last of those is what makes slip
observable in the timeline at all, since slip moves neither the clip nor its
length. The **Program panel docks on the right** and shows the picture at the
playhead, which is the other way to see a slip: the frame changes and nothing
moves. It follows scrubs, steps and edits but does **not** sustain playback
(D30).

**Mouse:** click a clip to select it, drag it to move it, drag the ruler strip at
the top to move the playhead.

To see the layout without a display:

```powershell
$env:QT_QPA_PLATFORM='offscreen'
.\build\windows-release\bin\reelforge.exe --demo-timeline --screenshot out.png
```

No test can see that a panel has covered the application; two such defects
reached the project owner before this existed.

### Measuring the preview

```powershell
.\build\windows-release\bin\rf_render_bench.exe --source <file> --frames 120
.\build\windows-release\bin\rf_render_bench.exe --source <file> --frames 120 --readback
```

Reports the per-frame cost **and how many frames the decoders materialised** --
the counter is the important half, because it distinguishes "this is expensive"
from "this is seeking back to a keyframe every frame". On the reference machine
at 1080x1920: one layer p50 4.67 ms, three layers p50 13.86 ms, budget 33.33 --
M3's own gate workload with 2.4x of headroom (ADR 019).

**The counters lie about seeking, and that cost two iterations.**
`frames_materialised` and `frames_decoded` both read a healthy 1.0 per frame
while `seek_to_frame` was eating 23.5 of every 29.2 ms. Use `--breakdown` to time
the stages, and `seeks()` for the seek question specifically.

### Seeing a timeline frame render

```powershell
.\build\windows-release\bin\reelforge.exe --demo-timeline --render-frame 5 --out frame5.png
```

The whole real path: model, decoder, converter, compositor. `testsrc2` burns its
frame number into the picture, so this doubles as an oracle for the source-frame
arithmetic -- clip 1 begins twenty frames in, so timeline frame 5 must read
`25` and frame 30 must read `30` (ADR 018).

**Keys, Adobe's own** (ADR 016, read from their page): `Ctrl+Alt+←/→` slips,
`Alt+,`/`Alt+.` slides, `Alt+←/→` nudges, `←`/`→` step the playhead, `Space`
plays. All act on the selected clip with **no tool needed**. Add `Shift` for five
frames. `L`, `J`, `K` shuttle — the playhead only, because nothing decodes at the
shuttle rate fast enough to keep up (D30).

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
- **D30** — the Program panel shows the picture at the playhead and presents it
  through the swapchain where it can (confirmed to execute on the reference
  machine, i17), but it renders one frame per scrub and decodes per frame, so it
  does not *sustain* playback: pressing `L` still sweeps a playhead faster than a
  picture can follow. The remaining half of D25.
- **D35/D36** — both resolved in i17, and both are about a check that could not
  fail. Worth reading in `BACKLOG.md` before writing another one.
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
- **A Windows path written into a document through a shell heredoc will be
  eaten.** `\build\windows-release` came out as a backspace, `uild`, a
  backspace and `in` — twice, in two different files, because the backslashes
  were consumed as escapes before the script ever ran. Build such strings from
  `chr(92)` rather than from literals, and grep the result for control
  characters afterwards; the damage is invisible in a diff summary and reads as
  success.
