# rf_edit is the interaction model: tools, selection, key chords and the command
# map that binds them to editing actions.
#
# It depends on rf_core and rf::timeline. It must never depend on Qt -- that is
# what lets M4's "driven by keyboard only" gate be a headless test rather than a
# screenshot, and it keeps the key-to-edit path exercisable on a CI runner with
# no display.

add_library(rf_edit STATIC
    action.cpp
    command_map.cpp
    editor.cpp
    key.cpp
    "${PROJECT_SOURCE_DIR}/include/rf/edit/action.hpp"
    "${PROJECT_SOURCE_DIR}/include/rf/edit/command_map.hpp"
    "${PROJECT_SOURCE_DIR}/include/rf/edit/editor.hpp"
    "${PROJECT_SOURCE_DIR}/include/rf/edit/key.hpp")

add_library(rf::edit ALIAS rf_edit)

target_include_directories(rf_edit PUBLIC "${PROJECT_SOURCE_DIR}/include")

target_link_libraries(rf_edit
    PUBLIC rf::core rf::timeline
    PRIVATE rf::warnings)
