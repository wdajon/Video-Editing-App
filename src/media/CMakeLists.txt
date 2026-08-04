# rf_media owns everything that touches libav*. Nothing above this layer may
# include a libav header -- see docs/adr/004-ffmpeg-linkage.md. The FFmpeg
# include directories are PRIVATE precisely so that rule is enforced by the
# build rather than by review.
#
# The time model (rational.cpp) is deliberately free of libav: exact rational
# arithmetic is the foundation frame accuracy rests on, and it must be testable
# without a media file, a decoder, or a codec build.

find_package(FFMPEG REQUIRED)

add_library(rf_media STATIC
    libav_error.cpp
    media_info.cpp
    probe.cpp
    rational.cpp
    libav_error.hpp
    "${PROJECT_SOURCE_DIR}/include/rf/media/media_info.hpp"
    "${PROJECT_SOURCE_DIR}/include/rf/media/probe.hpp"
    "${PROJECT_SOURCE_DIR}/include/rf/media/rational.hpp")

add_library(rf::media ALIAS rf_media)

target_include_directories(rf_media PUBLIC "${PROJECT_SOURCE_DIR}/include")

# SYSTEM so that libav's own headers are exempt from the project warning policy;
# ReelForge code is not.
target_include_directories(rf_media SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})

target_link_libraries(rf_media
    PUBLIC rf::core
    PRIVATE rf::warnings ${FFMPEG_LIBRARIES})

target_link_directories(rf_media PRIVATE ${FFMPEG_LIBRARY_DIRS})
