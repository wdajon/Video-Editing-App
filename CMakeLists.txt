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

option(RF_BUILD_TESTS        "Build the ReelForge test suite"      ON)
option(RF_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"   ON)
option(RF_ENABLE_ASAN        "Build with AddressSanitizer"         OFF)
option(RF_ENABLE_TSAN        "Build with ThreadSanitizer"          OFF)
option(RF_ENABLE_UBSAN       "Build with UndefinedBehaviorSanitizer" OFF)

include(ReelForgeWarnings)
include(ReelForgeSanitizers)
include(ReelForgeGitVersion)

rf_resolve_git_revision(RF_GIT_REVISION RF_GIT_DIRTY)
message(STATUS "ReelForge ${PROJECT_VERSION} (git ${RF_GIT_REVISION}${RF_GIT_DIRTY})")

add_subdirectory(src/core)
add_subdirectory(tools/rf_version)

if(RF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
