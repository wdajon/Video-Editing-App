set(CMAKE_AUTOMOC ON)

add_library(rf_app STATIC
    main_window.cpp
    window_title.cpp
    "${PROJECT_SOURCE_DIR}/include/rf/app/main_window.hpp"
    "${PROJECT_SOURCE_DIR}/include/rf/app/window_title.hpp")

add_library(rf::app ALIAS rf_app)

target_include_directories(rf_app PUBLIC "${PROJECT_SOURCE_DIR}/include")

target_link_libraries(rf_app
    PUBLIC
        rf::core
        Qt6::Widgets
    PRIVATE
        rf::warnings)

add_executable(reelforge main.cpp)

target_link_libraries(reelforge
    PRIVATE
        rf::app
        rf::warnings)

# WIN32_EXECUTABLE is deliberately off: a console-attached build lets CI and
# crash triage see stderr, which is where RF_CHECK failures go. Revisit when
# there is a real crash reporter to route them to (M12).
set_target_properties(reelforge PROPERTIES OUTPUT_NAME "reelforge")

rf_deploy_qt(reelforge)

# The shell also gets the offscreen platform plugin so CI can start the real
# application on a machine with no display and prove it initialises, rather than
# only proving it links.
rf_deploy_qt_offscreen_platform(reelforge)
