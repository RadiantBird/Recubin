#pragma once

#include <filesystem>
#include <string>
#include <variant>

namespace StudioMain {

void showInternalIoFailure(
    const std::string& operation,
    const std::filesystem::path& path,
    const std::string& reason);

struct LaunchOptions {
    std::filesystem::path executablePath;
    std::filesystem::path autosaveRoot;
    std::filesystem::path settingsPath;
    std::string initialScenePath;
};

std::variant<int, LaunchOptions> prepareLaunch(int argc, char* argv[]);
int runStudio(const LaunchOptions& options);

} // namespace StudioMain
