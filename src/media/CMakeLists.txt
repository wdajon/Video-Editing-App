# rf_media owns everything that touches libav*. Nothing above this layer may
# include a libav header -- see docs/adr/004-ffmpeg-linkage.md.
#
# The time model below is deliberately free of libav: exact rational arithmetic
# is the foundation frame accuracy rests on, and it must be testable without a
# media file, a decoder, or a codec build.

add_library(rf_media STATIC
    rational.cpp
    "${PROJECT_SOURCE_DIR}/include/rf/media/rational.hpp")

add_library(rf::media ALIAS rf_media)

target_include_directories(rf_media PUBLIC "${PROJECT_SOURCE_DIR}/include")

target_link_libraries(rf_media
    PUBLIC rf::core
    PRIVATE rf::warnings)
