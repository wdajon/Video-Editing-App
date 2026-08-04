#include "rf/core/version.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace {

TEST(Version, MatchesCMakeProjectVersion) {
    // Guards against the generated header drifting from the project() call.
    EXPECT_EQ(rf::kVersionString,
              std::to_string(rf::kVersionMajor) + "." + std::to_string(rf::kVersionMinor) + "." +
                  std::to_string(rf::kVersionPatch));
}

TEST(Version, BuildInfoIsFullyPopulated) {
    const rf::BuildInfo info = rf::build_info();
    EXPECT_FALSE(info.version.empty());
    EXPECT_FALSE(info.git_revision.empty());
    EXPECT_FALSE(info.build_type.empty());
    EXPECT_FALSE(info.compiler.empty());
    EXPECT_FALSE(info.target_system.empty());
}

TEST(Version, ReproducibilityFollowsRevision) {
    const bool unknown = rf::kGitRevision == "unknown";
    const bool dirty = rf::kGitRevision.find("+dirty") != std::string_view::npos;
    EXPECT_EQ(rf::build_is_reproducible(), !unknown && !dirty)
        << "revision: " << rf::kGitRevision;
}

}  // namespace
