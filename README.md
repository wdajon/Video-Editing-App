# ReelForge

A cross-platform non-linear video editor for short-form vertical delivery
(Instagram Reels, Stories, 4:5 feed), written in C++20.

**Status: M0, iteration 1. Early. Nothing here edits video yet.** See
[docs/PROGRESS.md](docs/PROGRESS.md) for exactly what exists and what does not.

## Building

Requirements:

- A C++20 compiler (MSVC 19.4x, Clang 16+, or GCC 13+)
- CMake ≥ 3.24 and Ninja
- A bootstrapped [vcpkg](https://github.com/microsoft/vcpkg) checkout
- Qt 6.10.3 (see [ADR 003](docs/adr/003-qt-acquisition.md) for why it is pinned
  and why it is not in the vcpkg manifest)

Install Qt with `scripts/install_qt.ps1`, then point `QT_ROOT` at it:

```powershell
.\scripts\install_qt.ps1 -Prefix A:\Qt
$env:QT_ROOT = 'A:\Qt\6.10.3\msvc2022_64'
```

Configure with `-DRF_BUILD_APP=OFF` to build the headless targets without Qt.

```bash
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh          # bootstrap-vcpkg.bat on Windows
export VCPKG_ROOT=$PWD/vcpkg
```

Then, from the repository root:

```bash
cmake --preset linux-debug
cmake --build build/linux-debug --parallel
ctest --preset linux-debug
```

On Windows, `scripts/dev_env.ps1` locates the MSVC toolchain via `vswhere` and
puts the Build Tools copies of CMake and Ninja on `PATH`:

```powershell
. .\scripts\dev_env.ps1 -VcpkgRoot A:\vcpkg
cmake --preset windows-debug
cmake --build build\windows-debug --parallel
ctest --preset windows-debug
```

Available presets: `windows-debug`, `windows-release`, `windows-asan`,
`linux-debug`, `linux-release`, `linux-asan`, `linux-tsan`.

## Layout

| Path | Contents |
|---|---|
| `include/rf/` | Public headers, namespaced by module |
| `src/core/` | Error handling, invariant checks, build identity |
| `tools/` | Headless executables (CI smoke tests, later: render and bench) |
| `tests/` | GoogleTest suites, mirroring the `src/` layout |
| `cmake/` | Warning policy, sanitizer policy, git version resolution |
| `docs/SPECS.md` | Platform delivery specs, every value dated and sourced |
| `docs/adr/` | Architecture decision records |

## Ground rules

- **No platform spec number appears in C++.** Delivery limits live in
  `presets/*.json`, validated against a schema at startup, and every value is
  traceable to a dated source in [docs/SPECS.md](docs/SPECS.md).
- **No exceptions across module boundaries.** Fallible operations return
  `rf::Result<T>`; see [ADR 002](docs/adr/002-error-handling.md).
- **Zero warnings.** `-Wall -Wextra -Werror` / `/W4 /WX` on every target.

## Licence

GPL-3.0-or-later (inherited from the FFmpeg configuration ReelForge links).
