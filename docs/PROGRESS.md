# Progress

## M1 — Media engine: probe, decode, frame-accurate seek

**Exit gate:** seek to any frame of a 10-minute 4K file; decoded hash matches a
reference. **Gate status: NOT PASSED** — iteration 1 lays the time model the
gate depends on; nothing decodes yet.

### Iteration 3 — sequential decode and frame hashing

**Increment:** walk a file's video stream frame by frame, and hash frames
reproducibly. Linear decode is not just a feature — it is the **oracle** the
next increment's seek accuracy is measured against.

**Falsifiable check:** a 60-frame fixture yields exactly 60 frames; NTSC frame
spacing is exactly 1001 ticks in a 1/30000 base with no drift across the file;
decoding the same file twice yields identical hashes; and all 60 hashes are
distinct.

**Verified (2026-08-04, MSVC 19.44.35228, FFmpeg 8.1.2, Debug):**

```
100% tests passed, 0 tests failed out of 130
app = 10 tests    core = 48 tests    media = 72 tests
```

**Design decisions worth recording:**

- **Draining is explicit.** Codecs with B-frames hold frames back until told the
  stream ended. A decoder that omits the null-packet flush loses the last frames
  of every file, and loses them *silently*. `DecodesEveryFrameIncludingTheOnes
  HeldBackForReordering` exists to fail if that flush is ever removed.
- **End of stream is `Result<optional<VideoFrame>>`.** Running out of frames is
  an outcome, not a fault; making it an error code would force every caller to
  special-case a sentinel and would eventually get mistaken for a real failure.
- **Frames are tightly packed** (`av_image_copy_to_buffer`, alignment 1). Stride
  padding is uninitialised memory that varies by platform and decoder build; if
  it reached the hash, the seek oracle would produce false mismatches and there
  would be no way to tell them from real ones.
- **`best_effort_timestamp` over raw `pts`,** because raw pts is frequently
  absent on the first frames of a stream.
- **Every libav resource has an RAII owner** and there are no bare `av_*_free`
  calls in `decoder.cpp`. The error paths are numerous and each one returns
  early; this is what keeps them leak-free without per-path cleanup.
- **`describe_stream` was lifted out of `probe.cpp` into a shared internal
  unit.** The decoder must describe its stream identically to the probe;
  two translations drifting apart would mean a file reports one frame rate when
  inspected and a different one when played.

**Two tests exist to stop the next increment passing vacuously.**
`FrameHash.IsDeterministicAcrossDecodes` and `FrameHash.DistinguishesDifferent
Frames` establish that the hash actually separates frames. Without them, a seek
test comparing hashes could pass while proving nothing at all.

### Iteration 2 — probe

**Increment:** open a container, describe its streams, close it. No decoding, no
seeking. The first code in the project that links libav.

**Falsifiable check:** probing three real files reports exactly what `ffprobe`
reports for them, field by field, including a 30000/1001 frame rate surviving as
an exact ratio; and every malformed-input path returns a usable error instead of
crashing or returning a plausible-looking empty result.

**Verified (2026-08-04, MSVC 19.44.35228, FFmpeg 8.1.2, Debug):**

```
[11/21] Building CXX object src\media\CMakeFiles\rf_media.dir\probe.cpp.obj
[16/21] Linking CXX executable bin\rf_media_tests.exe     <- zero warnings at /W4 /WX

100% tests passed, 0 tests failed out of 113
app = 10 tests    core = 48 tests    media = 55 tests
```

**Design decisions worth recording:**

- **Unknown is `std::optional`, never a sentinel or a default.** Audio streams
  really do report `0/0` for frame rate; some containers leave aspect ratio at
  `0/1`. Those arrive as `nullopt`, so nothing downstream can consume an invented
  25 or 30 fps as though the file had stated it. A test asserts this specifically.
- **A zero time base is left at zero** rather than defaulted to something
  plausible, so a caller can detect an unusable stream with `is_zero()` instead
  of silently computing wrong timestamps from a fabricated base.
- **`container_frame_count` is documented as untrusted.** Containers lie; the
  honest count comes from decoding. It is kept for diagnostics and cross-checks,
  and named so nobody seeks with it.
- **FFmpeg include directories are `PRIVATE`**, so the ADR 004 layering rule --
  nothing above `rf_media` includes a libav header -- is enforced by the build
  rather than by review.
- **libav errors keep both the text and the numeric code.** `av_strerror` alone
  frequently yields "Invalid argument", which is not actionable in a bug report.

**Fixture provenance is recorded and its limit stated.** `tests/fixtures/media/`
carries small committed files with the exact `ffmpeg` command that produced each
one, plus the `ffprobe` output the test expectations are drawn from. They are
committed rather than generated at test time because a test that needs an
`ffmpeg` binary on the machine fails for reasons unrelated to ReelForge.

These fixtures are **adequate for probe and demux tests and explicitly not
adequate for the M1 exit gate**: they were produced by the FFmpeg command-line
tool, and a decoder cannot be its own oracle. The gate needs real footage or a
reference hash from an independent decoder. Until it has one, the gate is not
met and will not be claimed as met.

### Iteration 1 — exact rational time arithmetic

**Increment:** the timestamp representation, before any libav code. Frame
accuracy is decided by the number type, not by the decoder: 1001/30000 is not
representable in binary floating point, so a float pipeline accumulates error
and lands a cut a frame off after a few thousand frames. That is the ±1 drift
the rubric forbids, and no amount of care downstream fixes a representation that
permits it.

**Falsifiable check:** every frame of a 10-minute 29.97 fps timeline converts to
a 1/90000 tick base and back to exactly the frame it started on — all 17,982 of
them, not a sample.

**Verified (2026-08-04, MSVC 19.44.35228, Debug):**

```
[4/17]  Building CXX object src\media\CMakeFiles\rf_media.dir\rational.cpp.obj
[10/17] Linking CXX executable bin\rf_media_tests.exe        <- zero warnings at /W4 /WX

100% tests passed, 0 tests failed out of 100
app = 10 tests    core = 48 tests    media = 42 tests
```

**Design decisions worth recording:**

- `Rational` is always gcd-normalised with a positive denominator, so equality is
  structural: `24000/1001 == 48000/2002` without a comparison epsilon. Every
  "is this 23.976?" check in the codebase depends on that being reliable.
- Ordering cross-multiplies at 128-bit width. In int64 it silently gives the
  wrong answer for large values; in floating point it gives the wrong answer for
  near-equal ones.
- `mul_div` and `rescale` carry a full 64x64→128 intermediate, hand-written from
  32-bit limbs rather than `_umul128` or `unsigned __int128`. One implementation
  to reason about and test on every target beats a platform split in the code
  every timestamp flows through, and this runs at frame rate, not pixel rate.
- Overflow is always an error value, never a wrap. A wrapped timestamp is a seek
  to the wrong frame that reports success — the exact failure mode the rubric's
  "silently swallow an error" question is asking about.
- `approximate()` is deliberately not called `to_double`, and is not an implicit
  conversion, so using a float inside timing logic is visible at the call site.

**Environmental defect found and guarded (from the log, not from a guess):**

FFmpeg's vcpkg build runs under MSYS/autotools and passes the vcpkg library
directory to `link.exe` as an **unquoted** `-libpath`:

```
link.exe ... -libpath:A:/Development/Claude/Video Editing app/build/...
LINK : fatal error LNK1181: cannot open input file 'Editing.obj'
```

The checkout path contains spaces, the argument splits, and `Editing` becomes a
phantom object file. Worked around with
`-DVCPKG_INSTALLED_DIR=<space-free path>`; FFmpeg then builds in ~7 minutes. A
guard now runs **before** `project()` and fails in a second with both remedies
named, rather than letting it surface minutes deep inside a vcpkg install.

**Process note:** the first configure after this fix still failed, because CMake
files were edited while a configure was in flight and it read a mixed state. Not
a code defect, but a repeat-able way to waste a cycle: leave the tree alone while
a configure runs.



## M0 — Repo, CMake, dep manifest, CI, empty Qt shell

**Exit gate:** clean clone → build → run on 2 OSes; CI green.
**Gate status: PASSED** — 2026-08-04, run
[30891518538](https://github.com/wdajon/Video-Editing-App/actions/runs/30891518538)
at `6a93924`, attempt 1, all six jobs green from a clean checkout:

| Job | Result | Duration |
|---|---|---|
| Windows x64 Debug | success | 2.5 min |
| Windows x64 Release | success | 2.6 min |
| Linux x64 Debug | success | 1.8 min |
| Linux x64 Release | success | 1.7 min |
| Linux ASan+UBSan | success | 1.6 min |
| Linux TSan | success | 1.7 min |

Every job runs configure → build → `ctest` → both binaries executed, the GUI
shell under the offscreen platform so a green run means it initialises rather
than merely links.

### Iteration 1 — build system and core error handling

**Increment:** a buildable, tested C++20 skeleton with the error-handling
foundation every later module returns through, plus the dependency manifest and
warning/sanitizer policy. Deliberately excludes Qt: acquiring Qt is a separate
decision (ADR 001) and bundling it into this iteration would have meant reporting
"the build works" without ever having run it.

**Falsifiable check:** configure → build at `/W4 /WX` with zero warnings → 48
GoogleTest cases pass in both Debug and RelWithDebInfo → the smoke binary runs
and prints build identity.

**Verified (2026-08-04, MSVC 19.44.35228, CMake 3.31.6, Ninja 1.12.1, vcpkg 2026-07-27):**

- `cmake --build build/windows-debug --parallel` — 11 targets, zero warnings, zero errors
- `ctest --preset windows-debug` — 48/48 passed, 0 failed, 0 skipped, 1.43 s
- `ctest --preset windows-release` — 48/48 passed, 0 failed, 0 skipped, 1.03 s
- `build/windows-release/bin/rf_version.exe` — runs, prints version/compiler/target

**Scores:**

| # | Rubric item | Score | Justification |
|---|---|---|---|
| 1 | Correctness | 4 | 48 executed tests, including death tests for every misuse path of `Result`. Not 5: the code has never been compiled by GCC or Clang, and the non-MSVC warning set is materially stricter (`-Wconversion`, `-Wold-style-cast`, `-Wsign-conversion`). Portability is asserted, not demonstrated. |
| 2 | Frame accuracy | n/a | No media path in this increment. |
| 3 | Performance | n/a | No render path in this increment. No budget applies yet. |
| 4 | Robustness | 4 | Failure is a value everywhere; misuse aborts loudly instead of returning a plausible default; `dev_env.ps1` fails with an actionable message on every missing prerequisite. Not 5: defect D1 — the embedded git revision can be stale. |
| 5 | UI fidelity | n/a | No UI in this increment. |
| 6 | Architecture | 4 | `rf_core` has zero third-party dependencies and no knowledge of Qt, libav, or the GPU; everything is testable headlessly. Not 5: there is not yet enough structure for the layering rule to have been tested by anything. |
| 7 | Spec integrity | 4 | `docs/SPECS.md` records every value with a dated primary source and resolves the three named disputes with stated reasoning. Not 5: the 4:5 feed section is `UNSPECIFIED`, and no preset file or schema exists yet to hold the values. |

**Checklist:**

| Question | Answer |
|---|---|
| Does any code path silently swallow an error or return a default on failure? | **No.** `Result` is `[[nodiscard]]` at class scope; `value()` on a failed result aborts (`ResultDeath.ValueOnErrorAborts`). |
| Any `TODO`, stub, hardcoded path, or fake success? | **No.** No `TODO` markers; no hardcoded toolchain paths (`vswhere` discovery); `rf_version` does real work. |
| Does any test assert only that a call did not crash? | **No.** Every test asserts a specific value, message, or abort message. |
| Does any spec number appear in `.cpp`/`.h`? | **No.** No delivery specs are implemented yet; `docs/SPECS.md` holds them and `presets/` does not exist. |
| Does this increment leak, race, or allocate on the render thread? | **No render thread exists yet.** Unverified by tooling: ASan/TSan presets are defined but have not been run — see D3. |
| Would a first-time Premiere user find this control where they expect it? | **n/a.** No UI. |

### Iteration 2 — Qt application shell

**Increment:** the Qt 6 shell M0 asks for — a real window with a real menu bar
and status bar — plus the acquisition path that makes Qt reproducible on a clean
machine and in CI. Deliberately no empty dock widgets standing in for the M4
panels: a panel that docks but shows nothing is indistinguishable from a broken
panel.

**Falsifiable check:** clean configure finds Qt through `QT_ROOT` → build at
`/W4 /WX` with zero warnings → 58 tests pass in Debug and RelWithDebInfo,
including widget tests that construct a real `MainWindow` with no display server
→ `reelforge.exe` starts, shows a titled window, and exits 0 on window close.

**Verified (2026-08-04, Qt 6.10.3, MSVC 19.44.35228):**

- `cmake --build build/windows-debug --parallel` — 21 targets, zero warnings
- `ctest --preset windows-debug` — 58/58 passed (48 core + 10 app), 2.03 s
- `ctest --preset windows-release` — 58/58 passed, 3.66 s
- `reelforge.exe` — ran 4 s, window title `ReelForge`, 54.3 MB working set,
  **0% CPU while idle** (budget: < 2%), closed cleanly with exit code 0

**Scores:**

| # | Rubric item | Score | Justification |
|---|---|---|---|
| 1 | Correctness | 4 | 58 executed tests; widget behaviour is asserted, not assumed. Still no GCC/Clang compile — unchanged from iteration 1. |
| 2 | Frame accuracy | n/a | No media path. |
| 3 | Performance | 5 | The only budget applicable to this increment — idle CPU < 2% — is met at 0%, measured over 3 s. |
| 4 | Robustness | 4 | Every failure path in the Qt plumbing produces an actionable message (missing Qt names `scripts/install_qt.ps1`; missing `windeployqt` and missing offscreen plugin each fail configuration rather than producing a binary that cannot start). Unchanged defect D1. |
| 5 | UI fidelity | 3 | Title bar follows Premiere's `App - path *` convention and Quit has a keyboard shortcut. That is the whole of the UI. There is no panel, no docking, no workspace, no JKL — all M4. Scored against what a Premiere user would expect to *find*, this is honestly a 3. |
| 6 | Architecture | 5 | The layering rule now has something to test it: `rf_core` does not link Qt, `window_title.cpp` holds the title format with no Qt dependency and is unit-tested without one, and `rf_app` is the only target that knows Qt exists. |
| 7 | Spec integrity | 4 | Unchanged from iteration 1; no new platform-derived numbers were introduced. |

**Checklist:** no silent error swallowing · no TODOs or stubs (the window has no
placeholder panels) · no test asserts merely "didn't crash" · no spec number in
C++ · no render thread yet; ASan/TSan still unrun (D3) · a Premiere user would
find File > Exit where they expect it and nothing else, because nothing else
exists yet.

**Two real defects found and fixed by running things rather than reading them:**

1. Qt 6.12.0 and 6.11.x cannot be installed at all by the current `aqtinstall`;
   the pin moved to 6.10.3 with the measured boundary recorded in ADR 003.
2. `windeployqt` deploys only the `qwindows` platform plugin, so the widget test
   binary aborted at startup with exit code 3 and no assertion output. Fixed by
   deploying `Qt6::QOffscreenIntegrationPlugin` explicitly. Had the test binary
   relied on a CTest environment property instead, discovery would have failed
   the same way — noted in `tests/app/main.cpp`.

### Iteration 3 — first CI run against real Linux

**Increment:** push to a remote and let the six-job matrix compile this code with
a compiler other than MSVC for the first time.

**Result — the prediction was wrong, in the good direction.** Iterations 1 and 2
both recorded an expectation that Clang would reject code MSVC accepted, because
`-Wconversion`, `-Wsign-conversion` and `-Wold-style-cast` had never run against
it. All four Linux jobs passed on the first attempt, including the moc-generated
sources that were called out as the most likely casualty:

```
[success] Linux x64 Debug        [success] Linux ASan+UBSan
[success] Linux x64 Release      [success] Linux TSan
```

ASan+UBSan and TSan passing on real hardware retires the "sanitizers are defined
but have never been run" caveat carried since iteration 1 (D3).

**What did break was the CI script, not the code.** The Windows job failed before
compiling anything, in the Qt install step:

```
py7zr.exceptions.Bad7zFile: Specified path is bad:
  lib/cmake/Qt6Gui/Qt6QWebpPluginTargets-relwithdebinfo.cmake
```

The first hypothesis was a path defect: the workflow built the install prefix by
appending `/Qt` to a Windows `$GITHUB_WORKSPACE`, producing the mixed-separator
`D:\a\Video-Editing-App\Video-Editing-App/Qt`. That hypothesis was **wrong** —
the Windows Release job ran the identical step in the same workflow run and
succeeded. The failure is intermittent extraction inside aqtinstall's bundled
py7zr, not anything deterministic about our inputs.

Fixed by retrying the install up to three times, wiping the partially extracted
tree first because it is not resumable, and failing loudly if `Qt6Config.cmake`
is still absent afterwards. The same retry is applied to `scripts/install_qt.ps1`
and `scripts/bootstrap_linux.sh` so all three acquisition paths behave alike. The
mixed separator was tidied with `cygpath` as well — it was not the bug, but it
was sloppy.

**Method note:** the first fix was nearly applied on the strength of a plausible
reading of the log. Re-running the failed job first cost one command and showed
the theory was wrong. A deterministic-looking failure is not deterministic until
it has failed twice.

### Iteration 3a — a race in the build system, found by CI after the gate passed

The gate run at `6a93924` was green. The very next commit (`891d1d9`, an Actions
version bump) turned Windows Debug red at the **Build** step:

```
FAILED: [code=1] bin/rf_app_tests.exe
Cannot copy C:\Windows\system32\icuuc.dll to ...\build\windows-debug\bin\icuuc.dll:
  Cannot open for output: Existing file ...\bin\icuuc.dll is not writable
```

`rf_deploy_qt` was attached to both `reelforge` and `rf_app_tests`, which share a
single output directory. Under `--parallel`, two `windeployqt` invocations raced
to write the same DLLs. Deployment now happens once, on `reelforge`, and
`rf_app_tests` takes a build dependency on it so the two are ordered.

**Two things this exposes about the earlier iterations, both worth keeping:**

1. **Iteration 2's checklist answer was wrong.** The question "does this increment
   leak, race, or allocate on the render thread?" was answered "no render thread
   exists yet". There was no render thread, but there *was* a race — in the build
   system, shipped in the same increment. The question deserved a wider reading
   than it got.
2. **Local verification was invalid and looked fine.** Every local build reused a
   `bin/` that was already populated, so `windeployqt` reported "up to date" and
   never wrote anything, so the two invocations never contended. Only a clean
   tree exercises the copy. The fix was re-verified after
   `rm -rf build/windows-debug`, which is now the standard for anything touching
   the build system.

### Gate blockers, resolved

| Raised | Blocker | Resolution |
|---|---|---|
| i1 | Never built on a second OS | Linux Debug/Release green on `ubuntu-24.04` with Clang |
| i1 | CI never executed (D3) | Six-job matrix green; ASan, UBSan and TSan all run on real hardware |
| i1 | No Qt shell | Delivered in iteration 2 |

Local Linux verification was attempted first and abandoned on evidence: this
machine's Ryzen 9 5900X reports `VirtualizationFirmwareEnabled: False`, so AMD-V
is disabled in firmware and no hypervisor — WSL2 included — can start. CI runners
are the substitute. `scripts/bootstrap_linux.sh` remains the local path for
anyone whose firmware allows it.

### Milestone closed

M0 is complete. Remaining known defects (D1, D2, D4, D5) are tracked in
`docs/BACKLOG.md`; none of them gate M0, and each is scheduled against the
milestone that first depends on it.

### Next action

Begin M1 — media engine: probe, decode, frame-accurate seek. Exit gate: seek to
any frame of a 10-minute 4K file with the decoded hash matching a reference.
