// Test entry point for the Qt-dependent suites.
//
// Widget construction requires a live QApplication, so these tests cannot use
// gtest_main.

#include <QApplication>
#include <QByteArray>

#include <gtest/gtest.h>

int main(int argc, char** argv) {
    // Default to the offscreen QPA platform so the suite runs on a machine with
    // no display server. Set here rather than as a CTest environment property
    // because gtest_discover_tests also runs this binary to enumerate tests,
    // and that run does not inherit per-test environment settings -- it would
    // abort before reaching the first assertion. An explicit QT_QPA_PLATFORM
    // still wins, so a developer can watch the widgets if they want to.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    }

    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
