# Progress

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
