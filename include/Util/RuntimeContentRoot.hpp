#pragma once

#include <filesystem>
#include <optional>

enum class RuntimeContentPlatform { Windows, MacOS };

// Computes the packaged content root without touching process state.  The
// caller supplies whether startup.yaml exists so this remains easy to test.
std::optional<std::filesystem::path> resolveRuntimeContentRoot(
    const std::filesystem::path& executablePath,
    RuntimeContentPlatform platform,
    bool explicitScene,
    bool startupExists);
