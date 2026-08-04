# ADR 003 — Qt 6 acquisition and version pin

- **Status:** Accepted
- **Date:** 2026-08-04
- **Milestone:** M0
- **Amends:** [ADR 001](001-technology-stack.md), which anticipated this exception

## Context

ADR 001 puts every dependency in the vcpkg manifest. Qt is the exception it
flagged. Two forces pull against each other:

- **Reproducibility** says Qt belongs in `vcpkg.json` like everything else.
- **Cost** says otherwise: `qtbase` through vcpkg is a source build measured in
  hours per clean machine, paid again by every CI job without a binary cache.
  ReelForge will eventually pull in `qtbase`, and Qt is not the interesting part
  of this project — it is chrome around a render graph.

## Options

1. **vcpkg `qtbase` from source.** One manifest governs everything. 1–3 hours per
   clean machine; CI needs a populated binary cache before it is tolerable.
2. **Official Qt online installer.** Requires a Qt account and interactive
   sign-in — unusable in CI and unusable by an automated agent.
3. **`aqtinstall`.** Third-party CLI that downloads the *official* Qt binaries
   from `download.qt.io` without an account. This is what most Qt CI setups use.

## Decision

**`aqtinstall`, pinned to Qt 6.10.3, `win64_msvc2022_64`, installed to `A:\Qt`**
(Linux: `gcc_64`). Modules: `qtsvg`, `qtshadertools`, `qtimageformats`.

The artefacts are Qt's own official binaries; `aqtinstall` only replaces the
account-gated downloader.

### Why 6.10.3 and not the newest release

Qt 6.12.0 was the first choice — 6.8 was the first LTS and every fourth minor is
an LTS thereafter, which makes 6.12 the current LTS line. It does not install.
Measured on 2026-08-04 with `aqtinstall` 3.3.0 (the newest version published on
PyPI):

| Qt version | `aqt list-qt windows desktop --arch` |
|---|---|
| 6.8.3 | resolves → `win64_msvc2022_64` |
| 6.9.3 | resolves → `win64_msvc2022_64` |
| 6.10.3 | resolves → `win64_msvc2022_64` |
| 6.11.0 | fails: `Failed to download checksum for the file 'Updates.xml'` |
| 6.11.1 | fails: same |
| 6.12.0 | fails: same |

The failure is not network and not syntax: `download.qt.io` answers 200 for the
repository root, and the same command succeeds against 6.10.3 seconds later. The
boundary sits exactly between 6.10 and 6.11, which is `aqtinstall` not knowing
the repository layout Qt adopted at 6.11. There is no newer `aqtinstall` to
upgrade to.

So the choice is between the newest version that can actually be installed
reproducibly (6.10.3) and a version that can only be installed by hand through
an account-gated GUI (6.11+). A dependency a fresh CI runner cannot fetch is not
a dependency ReelForge can have.

## Consequences

- Qt's version is pinned in `scripts/install_qt.ps1` and in CI, **not** in
  `vcpkg.json`. That split is the cost of this decision; the pin is still exact
  and still in version control.
- `CMAKE_PREFIX_PATH` must point at the Qt install. Presets read it from the
  `QT_ROOT` environment variable so no absolute path is committed.
- **Revisit when `aqtinstall` supports Qt 6.11+**, and move to the 6.12 LTS line
  at that point. Tracked as D4 in `docs/BACKLOG.md`.
- ReelForge targets Qt 6.10 APIs. Nothing may depend on a 6.11+ API until the
  pin moves.
