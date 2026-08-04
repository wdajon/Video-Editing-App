// rf_version -- prints build identity and exits.
//
// This is the headless smoke target: CI runs it to prove the toolchain produced
// a binary that links against rf_core and runs on the target machine, without
// requiring a display server.

#include <cstdio>

#include "rf/core/version.hpp"

int main() {
    const rf::BuildInfo info = rf::build_info();

    std::printf("ReelForge %.*s\n", static_cast<int>(info.version.size()), info.version.data());
    std::printf("  revision:   %.*s\n", static_cast<int>(info.git_revision.size()),
                info.git_revision.data());
    std::printf("  build type: %.*s\n", static_cast<int>(info.build_type.size()),
                info.build_type.data());
    std::printf("  compiler:   %.*s\n", static_cast<int>(info.compiler.size()),
                info.compiler.data());
    std::printf("  target:     %.*s\n", static_cast<int>(info.target_system.size()),
                info.target_system.data());
    std::printf("  reproducible build: %s\n", rf::build_is_reproducible() ? "yes" : "no");

    return 0;
}
