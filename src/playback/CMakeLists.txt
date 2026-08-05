# rf_playback owns the playhead and the frame accounting M3's gate is stated in.
#
# It depends on rf_core and on rf::media for Rational, and on nothing else. No
# GPU, no decoder, no Qt: a 60-second sustained-playback scenario has to be a
# unit test that runs in milliseconds, or it will not be run often enough to
# catch a regression.

add_library(rf_playback STATIC
    frame_log.cpp
    playback_clock.cpp
    "${PROJECT_SOURCE_DIR}/include/rf/playback/clock.hpp"
    "${PROJECT_SOURCE_DIR}/include/rf/playback/frame_log.hpp"
    "${PROJECT_SOURCE_DIR}/include/rf/playback/playback_clock.hpp")

add_library(rf::playback ALIAS rf_playback)

target_include_directories(rf_playback PUBLIC "${PROJECT_SOURCE_DIR}/include")

target_link_libraries(rf_playback
    PUBLIC rf::core rf::media
    PRIVATE rf::warnings)
