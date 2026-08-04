cmake_minimum_required(VERSION 3.24)

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

    find_package(Qt6 6.10 QUIET COMPONENTS Widgets)
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
add_subdirectory(tools/rf_version)

if(RF_BUILD_APP)
    add_subdirectory(src/app)
endif()

if(RF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
