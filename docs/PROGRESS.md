# Progress

## M0 — Repo, CMake, dep manifest, CI, empty Qt shell

**Exit gate:** clean clone → build → run on 2 OSes; CI green.
**Gate status: NOT PASSED.** See "Blocking the gate" below.

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

### Blocking the gate

1. **Never built on a second OS.** Windows only. Linux is configured for but
   unproven; expect `-Wconversion`/`-Wold-style-cast` fallout on first contact.
2. **CI has never run.** `.github/workflows/ci.yml` exists and is unexecuted —
   there is no git remote. "CI green" is currently an unsupported claim (D3).

Item 3 of iteration 1 — "no Qt shell" — is cleared. Both remaining items are the
same blocker wearing two hats: without a remote there is no CI, and without CI
there is no second OS. Nothing else in M0 is outstanding.

### Next action

Add the git remote once it exists, push, and fix whatever the Linux jobs report.
Until then the M0 gate stays open and is reported as open.
