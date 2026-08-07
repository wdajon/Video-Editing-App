# rf_render turns a document into pixels.
#
# The one place that joins the model, the decoder and the compositor. It is the
# export path's core as much as the Program monitor's: M5 renders the same way,
# one frame at a time, into an encoder rather than onto a screen -- so an
# exported file and a previewed frame cannot disagree about what the edit is.
#
# It must never depend on Qt. Rendering a frame is not a windowing concern, and
# keeping it out is what lets the render path be tested with no display.

add_library(rf_render STATIC
    sequence_renderer.cpp
    "${PROJECT_SOURCE_DIR}/include/rf/render/sequence_renderer.hpp")

add_library(rf::render ALIAS rf_render)

target_include_directories(rf_render PUBLIC "${PROJECT_SOURCE_DIR}/include")

target_link_libraries(rf_render
    PUBLIC rf::core rf::gpu rf::media rf::timeline
    PRIVATE rf::warnings)
