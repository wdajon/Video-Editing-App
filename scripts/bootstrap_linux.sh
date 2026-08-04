#!/usr/bin/env bash
#
# Brings a bare Debian/Ubuntu machine (or a fresh WSL distro) to the point where
# ReelForge builds and its tests pass. Mirrors what .github/workflows/ci.yml does
# on the Linux runners, so a local failure and a CI failure mean the same thing.
#
#   ./scripts/bootstrap_linux.sh                 # install deps, configure, build, test
#   ./scripts/bootstrap_linux.sh --deps-only     # stop after installing dependencies
#
# Not idempotent-hostile: re-running is safe and skips work already done.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VCPKG_DIR="${VCPKG_ROOT:-$HOME/vcpkg}"
QT_PREFIX="${QT_PREFIX:-$HOME/Qt}"
PRESET="${PRESET:-linux-debug}"
DEPS_ONLY=0

# Pinned by docs/adr/003-qt-acquisition.md. Must match scripts/install_qt.ps1
# and the RF_QT_VERSION in .github/workflows/ci.yml (see defect D5).
QT_VERSION="6.10.3"
QT_ARCH="linux_gcc_64"
QT_SUBDIR="gcc_64"

for arg in "$@"; do
    case "$arg" in
        --deps-only) DEPS_ONLY=1 ;;
        *) echo "unknown argument: $arg" >&2; exit 2 ;;
    esac
done

log() { printf '\n=== %s ===\n' "$1"; }

log "Installing build tools and Qt runtime dependencies"
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
    build-essential clang lld ninja-build cmake git curl zip unzip tar pkg-config \
    ca-certificates python3 python3-venv python3-pip \
    libgl1-mesa-dev libxkbcommon-dev libxkbcommon-x11-0 libvulkan-dev \
    libfontconfig1 libfreetype6 libx11-xcb1 libxcb-cursor0 libxcb-icccm4 \
    libxcb-image0 libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 \
    libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 libdbus-1-3

# CMake 3.24+ is required (CMakeLists.txt); Ubuntu 24.04 ships 3.28.
cmake_version="$(cmake --version | head -n1 | awk '{print $3}')"
log "cmake $cmake_version, $(clang --version | head -n1), ninja $(ninja --version)"

log "Bootstrapping vcpkg at $VCPKG_DIR"
if [ ! -d "$VCPKG_DIR/.git" ]; then
    git clone https://github.com/microsoft/vcpkg.git "$VCPKG_DIR"
fi
if [ ! -x "$VCPKG_DIR/vcpkg" ]; then
    "$VCPKG_DIR/bootstrap-vcpkg.sh" -disableMetrics
fi
export VCPKG_ROOT="$VCPKG_DIR"

# Ubuntu 24.04 marks the system Python as externally managed, so aqtinstall goes
# in a venv rather than fighting pip over it.
log "Installing Qt $QT_VERSION to $QT_PREFIX"
QT_ROOT_DIR="$QT_PREFIX/$QT_VERSION/$QT_SUBDIR"
if [ ! -f "$QT_ROOT_DIR/lib/cmake/Qt6/Qt6Config.cmake" ]; then
    AQT_VENV="$HOME/.cache/reelforge-aqt-venv"
    if [ ! -d "$AQT_VENV" ]; then
        python3 -m venv "$AQT_VENV"
    fi
    "$AQT_VENV/bin/pip" install --quiet --upgrade aqtinstall

    # aqtinstall's py7zr extraction fails intermittently part-way through (see
    # the comment in .github/workflows/ci.yml). A half-extracted tree is not
    # resumable, so every attempt starts from a clean directory.
    : "${QT_PREFIX:?refusing to delete an empty path}"
    for attempt in 1 2 3; do
        rm -rf "${QT_PREFIX:?}/$QT_VERSION"
        if "$AQT_VENV/bin/python" -m aqt install-qt linux desktop "$QT_VERSION" "$QT_ARCH" \
             -m qtshadertools qtimageformats -O "$QT_PREFIX"; then
            break
        fi
        if [ "$attempt" -eq 3 ]; then
            echo "aqt install-qt failed on all 3 attempts" >&2
            exit 1
        fi
        echo "aqt install-qt failed (attempt $attempt of 3), retrying" >&2
        sleep $((attempt * 15))
    done
fi
if [ ! -f "$QT_ROOT_DIR/lib/cmake/Qt6/Qt6Config.cmake" ]; then
    echo "Qt install finished but Qt6Config.cmake is missing under $QT_ROOT_DIR" >&2
    exit 1
fi
export QT_ROOT="$QT_ROOT_DIR"

if [ "$DEPS_ONLY" -eq 1 ]; then
    log "Dependencies ready"
    echo "export VCPKG_ROOT=$VCPKG_ROOT"
    echo "export QT_ROOT=$QT_ROOT"
    exit 0
fi

cd "$REPO_ROOT"

log "Configuring ($PRESET)"
cmake --preset "$PRESET"

log "Building ($PRESET)"
cmake --build "build/$PRESET" --parallel

log "Testing ($PRESET)"
ctest --preset "$PRESET"

log "Smoke run"
"build/$PRESET/bin/rf_version"
QT_QPA_PLATFORM=offscreen timeout 5s "build/$PRESET/bin/reelforge" || {
    status=$?
    # 124 is timeout(1) reporting that the app was still running when the clock
    # ran out, which is exactly what a working GUI shell should do.
    if [ "$status" -ne 124 ]; then
        echo "reelforge exited early with status $status" >&2
        exit 1
    fi
}

log "Linux build complete: $PRESET"
