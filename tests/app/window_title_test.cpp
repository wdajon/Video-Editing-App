#include "rf/app/window_title.hpp"

#include <gtest/gtest.h>

namespace {

using rf::app::compose_window_title;

TEST(WindowTitle, NoProjectIsApplicationNameAlone) {
    EXPECT_EQ(compose_window_title("ReelForge", "", false), "ReelForge");
}

TEST(WindowTitle, NoProjectIgnoresModifiedFlag) {
    // There is nothing to have modified, so no marker may appear -- a stray
    // asterisk on an empty editor reads as unsaved work that does not exist.
    EXPECT_EQ(compose_window_title("ReelForge", "", true), "ReelForge");
}

TEST(WindowTitle, OpenProjectFollowsPremiereConvention) {
    EXPECT_EQ(compose_window_title("ReelForge", "C:\\edits\\promo.rfproj", false),
              "ReelForge - C:\\edits\\promo.rfproj");
}

TEST(WindowTitle, ModifiedProjectIsMarked) {
    EXPECT_EQ(compose_window_title("ReelForge", "C:\\edits\\promo.rfproj", true),
              "ReelForge - C:\\edits\\promo.rfproj *");
}

TEST(WindowTitle, UnsavedProjectUsesCallerSuppliedName) {
    EXPECT_EQ(compose_window_title("ReelForge", "Untitled", true), "ReelForge - Untitled *");
}

TEST(WindowTitle, PreservesNonAsciiPaths) {
    // Project paths come from the filesystem and are UTF-8; the composer must
    // not truncate or transform them.
    EXPECT_EQ(compose_window_title("ReelForge", "D:\\Проекты\\ролик.rfproj", false),
              "ReelForge - D:\\Проекты\\ролик.rfproj");
}

}  // namespace
