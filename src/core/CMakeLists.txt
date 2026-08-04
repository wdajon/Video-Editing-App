configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/version.hpp.in"
    "${CMAKE_BINARY_DIR}/generated/rf/core/version.hpp"
    @ONLY)

add_library(rf_core STATIC
    assert.cpp
    error.cpp
    version.cpp
    "${CMAKE_BINARY_DIR}/generated/rf/core/version.hpp"
    "${PROJECT_SOURCE_DIR}/include/rf/core/assert.hpp"
    "${PROJECT_SOURCE_DIR}/include/rf/core/error.hpp"
    "${PROJECT_SOURCE_DIR}/include/rf/core/result.hpp")

add_library(rf::core ALIAS rf_core)

target_include_directories(rf_core
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
        "${CMAKE_BINARY_DIR}/generated")

# Build identity is injected here rather than baked into a checked-in header so
# that a build never reports a version it was not actually built as.
target_compile_definitions(rf_core PRIVATE
    RF_BUILD_TYPE="$<CONFIG>"
    RF_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}"
    RF_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
    RF_TARGET_SYSTEM="${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

target_link_libraries(rf_core
    PRIVATE rf::warnings
    PUBLIC rf::sanitizers)
