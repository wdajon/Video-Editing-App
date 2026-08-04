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
