#include "rf/app/window_title.hpp"

namespace rf::app {

namespace {
constexpr std::string_view kSeparator = " - ";
constexpr std::string_view kModifiedMarker = " *";
}  // namespace

std::string compose_window_title(std::string_view application,
                                 std::string_view project,
                                 bool modified) {
    if (project.empty()) {
        return std::string{application};
    }

    std::string title;
    title.reserve(application.size() + kSeparator.size() + project.size() +
                  kModifiedMarker.size());
    title.append(application);
    title.append(kSeparator);
    title.append(project);
    if (modified) {
        title.append(kModifiedMarker);
    }
    return title;
}

}  // namespace rf::app
