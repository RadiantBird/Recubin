#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <variant>

#include <Core/SdlGpuDriver.hpp>

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
    std::optional<std::filesystem::path> automationLayoutPath;
    // SDL GPU is selected only when the SDL Studio build is enabled.  An
    // absent value preserves the legacy OpenGL Studio launch path.
    std::optional<SdlGpuDriver> gpuDriver;
};

std::variant<int, LaunchOptions> prepareLaunch(int argc, char* argv[]);
int runStudio(const LaunchOptions& options);

} // namespace StudioMain
