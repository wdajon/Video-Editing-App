# rf_gpu owns every Vulkan type in the project.
#
# The Vulkan include directories are PRIVATE, so ADR 007's rule -- nothing above
# this layer includes a Vulkan header -- is enforced by the compiler rather than
# by review. That is what will make the eventual OpenGL fallback (D11) an
# extraction confined to one module instead of a search through the codebase.

find_package(VulkanHeaders CONFIG REQUIRED)
find_package(volk CONFIG REQUIRED)

add_library(rf_gpu STATIC
    device_info.cpp
    instance.cpp
    "${PROJECT_SOURCE_DIR}/include/rf/gpu/device_info.hpp"
    "${PROJECT_SOURCE_DIR}/include/rf/gpu/instance.hpp")

add_library(rf::gpu ALIAS rf_gpu)

target_include_directories(rf_gpu PUBLIC "${PROJECT_SOURCE_DIR}/include")

target_link_libraries(rf_gpu
    PUBLIC rf::core
    PRIVATE rf::warnings volk::volk Vulkan::Headers)
