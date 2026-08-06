cmake_minimum_required(VERSION 3.24)

# FFmpeg is built by vcpkg through MSYS/autotools, and its configure step passes
# the vcpkg library directory to link.exe as an unquoted -libpath. A space in
# that path splits the argument, and the link fails with an error that names a
# fragment of your own directory as a missing object file:
#
#   -libpath:A:/Development/Claude/Video Editing app/build/...
#   LINK : fatal error LNK1181: cannot open input file 'Editing.obj'
#
# Caught before project() because the failure otherwise happens inside vcpkg's
# install during project(), several minutes in and with the cause well buried.
if(NOT DEFINED VCPKG_INSTALLED_DIR AND CMAKE_BINARY_DIR MATCHES " ")
    message(FATAL_ERROR
        "The build directory contains a space:\n"
        "  ${CMAKE_BINARY_DIR}\n"
        "FFmpeg cannot be built from a path containing spaces. Either:\n"
        "  - move the checkout to a path with no spaces (preferred), or\n"
        "  - configure with -DVCPKG_INSTALLED_DIR=<space-free path>, e.g.\n"
        "      cmake --preset windows-debug -DVCPKG_INSTALLED_DIR=C:/rf-deps/windows-debug")
endif()

project(ReelForge
    VERSION 0.1.0
    DESCRIPTION "Non-linear video editor for short-form vertical delivery"
    HOMEPAGE_URL "https://github.com/reelforge/reelforge"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Single output directory keeps DLL/plugin lookup sane on Windows and makes the
# headless tools findable from CI scripts without probing per-target paths.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

option(RF_BUILD_APP          "Build the Qt application shell"      ON)
option(RF_BUILD_TESTS        "Build the ReelForge test suite"      ON)
option(RF_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"   ON)
option(RF_ENABLE_ASAN        "Build with AddressSanitizer"         OFF)
option(RF_ENABLE_TSAN        "Build with ThreadSanitizer"          OFF)
option(RF_ENABLE_UBSAN       "Build with UndefinedBehaviorSanitizer" OFF)

include(ReelForgeWarnings)
include(ReelForgeSanitizers)
include(ReelForgeGitVersion)
include(ReelForgeQtDeploy)

rf_resolve_git_revision(RF_GIT_REVISION RF_GIT_DIRTY)
message(STATUS "ReelForge ${PROJECT_VERSION} (git ${RF_GIT_REVISION}${RF_GIT_DIRTY})")

if(RF_BUILD_APP)
    # Qt is installed by scripts/install_qt.ps1 rather than through vcpkg; see
    # docs/adr/003-qt-acquisition.md for why, and for why the version is pinned.
    if(DEFINED ENV{QT_ROOT})
        list(APPEND CMAKE_PREFIX_PATH "$ENV{QT_ROOT}")
    endif()

    # Test supplies QTest::keyClick, which is how M4's keyboard-only gate is
    # driven against a real widget rather than against the editor directly.
    find_package(Qt6 6.10 QUIET COMPONENTS Widgets Test)
    if(NOT Qt6_FOUND)
        message(FATAL_ERROR
            "Qt 6.10 with the Widgets component was not found.\n"
            "Install it with scripts/install_qt.ps1, then set QT_ROOT to the "
            "install prefix (the directory containing lib/cmake/Qt6), or "
            "configure with -DRF_BUILD_APP=OFF to build the headless targets "
            "only.\n"
            "CMAKE_PREFIX_PATH was: ${CMAKE_PREFIX_PATH}")
    endif()
    message(STATUS "Qt ${Qt6_VERSION} at ${QT6_INSTALL_PREFIX}")
endif()

add_subdirectory(src/core)
add_subdirectory(src/media)
add_subdirectory(src/gpu)
add_subdirectory(src/playback)
add_subdirectory(src/timeline)
add_subdirectory(src/edit)
add_subdirectory(tools/rf_gpu_info)
add_subdirectory(tools/rf_playback_bench)
add_subdirectory(tools/rf_seek_check)
add_subdirectory(tools/rf_version)

if(RF_BUILD_APP)
    add_subdirectory(src/app)
endif()

if(RF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
