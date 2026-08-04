#include "rf/core/version.hpp"

namespace rf {

BuildInfo build_info() noexcept {
    return BuildInfo{
        .version = kVersionString,
        .git_revision = kGitRevision,
        .build_type = RF_BUILD_TYPE,
        .compiler = RF_COMPILER_ID " " RF_COMPILER_VERSION,
        .target_system = RF_TARGET_SYSTEM,
    };
}

bool build_is_reproducible() noexcept {
    if (kGitRevision == "unknown") {
        return false;
    }
    return kGitRevision.find("+dirty") == std::string_view::npos;
}

}  // namespace rf
