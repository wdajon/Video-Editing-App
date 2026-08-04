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
    media/rational_test.cpp)

target_link_libraries(rf_media_tests
    PRIVATE
        rf::media
        rf::warnings
        GTest::gtest
        GTest::gtest_main)

gtest_discover_tests(rf_media_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS "media")

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
