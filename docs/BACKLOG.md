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
| D5 | M0 i2 | The Qt version pin exists in two files (`scripts/install_qt.ps1`, `.github/workflows/ci.yml`) with nothing enforcing that they agree. A drift between them means CI tests a different Qt than developers do. Fix: single source of truth read by both. | — |

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
