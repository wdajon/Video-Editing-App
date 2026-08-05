find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(rf_core_tests
    core/assert_test.cpp
    core/error_test.cpp
    core/result_test.cpp
    core/version_test.cpp)

target_link_libraries(rf_core_tests
    PRIVATE
        rf::core
        rf::warnings
        GTest::gtest
        GTest::gtest_main)

# PRE_TEST discovery keeps the test list correct when the binary is rebuilt but
# CMake is not re-run, which is the normal edit-build-test loop.
gtest_discover_tests(rf_core_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS "core")

add_executable(rf_media_tests
    media/decoder_test.cpp
    media/probe_test.cpp
    media/rational_test.cpp
    media/seek_test.cpp)

target_link_libraries(rf_media_tests
    PRIVATE
        rf::media
        rf::warnings
        GTest::gtest
        GTest::gtest_main)

# Fixtures are read from the source tree rather than copied into the build tree:
# they are inputs, they are never written, and a stale copy in a build directory
# is a debugging session nobody needs.
target_compile_definitions(rf_media_tests
    PRIVATE RF_TEST_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures")

gtest_discover_tests(rf_media_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS "media")

add_executable(rf_playback_tests
    playback/frame_log_test.cpp
    playback/playback_clock_test.cpp)

target_link_libraries(rf_playback_tests
    PRIVATE
        rf::playback
        rf::warnings
        GTest::gtest
        GTest::gtest_main)

gtest_discover_tests(rf_playback_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS "playback")

add_executable(rf_timeline_tests
    timeline/command_fuzz_test.cpp
    timeline/command_test.cpp
    timeline/document_test.cpp
    timeline/serialise_test.cpp)

target_link_libraries(rf_timeline_tests
    PRIVATE
        rf::timeline
        rf::warnings
        GTest::gtest
        GTest::gtest_main)

gtest_discover_tests(rf_timeline_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS "timeline")

if(RF_BUILD_APP)
    set(CMAKE_AUTOMOC ON)

    add_executable(rf_app_tests
        app/main.cpp
        app/main_window_test.cpp
        app/window_title_test.cpp)

    # No gtest_main: widgets need a QApplication, which app/main.cpp owns.
    target_link_libraries(rf_app_tests
        PRIVATE
            rf::app
            rf::warnings
            GTest::gtest)

    # Deliberately NOT calling rf_deploy_qt here. rf_app_tests and reelforge
    # share one output directory, and two concurrent windeployqt runs writing
    # the same DLLs race under a parallel build:
    #
    #   Cannot copy ...icuuc.dll: Existing file .../bin/icuuc.dll is not writable
    #
    # Deployment happens once, on reelforge. Depending on that target makes the
    # build system order the two, so the Qt runtime is in place beside the test
    # binary before CTest ever runs it.
    add_dependencies(rf_app_tests reelforge)

    gtest_discover_tests(rf_app_tests
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS "app")
endif()
