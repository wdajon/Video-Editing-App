# rf_timeline is the edit model: tracks, clips, and the state undo must restore.
#
# It depends on rf_core and on rf::media only for Rational. It must never depend
# on libav, Qt, or the GPU -- the model has to be exercisable by a fuzz test with
# no media, no window, and no device.

add_library(rf_timeline STATIC
    command.cpp
    composition.cpp
    document.cpp
    serialise.cpp
    trim.cpp
    "${PROJECT_SOURCE_DIR}/include/rf/timeline/command.hpp"
    "${PROJECT_SOURCE_DIR}/include/rf/timeline/composition.hpp"
    "${PROJECT_SOURCE_DIR}/include/rf/timeline/document.hpp"
    "${PROJECT_SOURCE_DIR}/include/rf/timeline/ids.hpp"
    "${PROJECT_SOURCE_DIR}/include/rf/timeline/serialise.hpp"
    "${PROJECT_SOURCE_DIR}/include/rf/timeline/trim.hpp")

add_library(rf::timeline ALIAS rf_timeline)

target_include_directories(rf_timeline PUBLIC "${PROJECT_SOURCE_DIR}/include")

target_link_libraries(rf_timeline
    PUBLIC rf::core rf::media
    PRIVATE rf::warnings)
