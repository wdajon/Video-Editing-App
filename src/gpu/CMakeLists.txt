# rf_gpu owns every Vulkan type in the project.
#
# The Vulkan include directories are PRIVATE, so ADR 007's rule -- nothing above
# this layer includes a Vulkan header -- is enforced by the compiler rather than
# by review. That is what will make the eventual OpenGL fallback (D11) an
# extraction confined to one module instead of a search through the codebase.

find_package(VulkanHeaders CONFIG REQUIRED)
find_package(volk CONFIG REQUIRED)

include(ReelForgeShaders)

add_library(rf_gpu STATIC
    compositor.cpp
    device.cpp
    device_info.cpp
    image.cpp
    instance.cpp
    texture.cpp
    device_impl.hpp
    instance_impl.hpp
    texture_impl.hpp
    vulkan_util.cpp
    vulkan_util.hpp
    "${PROJECT_SOURCE_DIR}/include/rf/gpu/compositor.hpp"
    "${PROJECT_SOURCE_DIR}/include/rf/gpu/device.hpp"
    "${PROJECT_SOURCE_DIR}/include/rf/gpu/device_info.hpp"
    "${PROJECT_SOURCE_DIR}/include/rf/gpu/image.hpp"
    "${PROJECT_SOURCE_DIR}/include/rf/gpu/instance.hpp"
    "${PROJECT_SOURCE_DIR}/include/rf/gpu/texture.hpp")

add_library(rf::gpu ALIAS rf_gpu)

# Compiled to SPIR-V at build time and embedded, so a broken shader fails the
# build rather than the user's first playback, and there is no load path to get
# wrong at runtime.
rf_add_shader(rf_gpu
    SOURCE "${PROJECT_SOURCE_DIR}/shaders/fill_pattern.comp"
    SYMBOL fill_pattern)

rf_add_shader(rf_gpu
    SOURCE "${PROJECT_SOURCE_DIR}/shaders/composite.comp"
    SYMBOL composite)

target_include_directories(rf_gpu PUBLIC "${PROJECT_SOURCE_DIR}/include")

# SYSTEM so libav-style third-party headers are exempt from the project warning
# policy; ReelForge code is not.
target_include_directories(rf_gpu SYSTEM PRIVATE ${Vulkan_INCLUDE_DIRS})

target_link_libraries(rf_gpu
    PUBLIC rf::core
    PRIVATE rf::warnings volk::volk Vulkan::Headers)
