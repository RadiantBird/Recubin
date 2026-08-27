#include <Util/RuntimeContentRoot.hpp>

std::optional<std::filesystem::path> resolveRuntimeContentRoot(
    const std::filesystem::path& executablePath,
    RuntimeContentPlatform platform,
    bool explicitScene,
    bool startupExists) {
    if (explicitScene || executablePath.empty() || !startupExists) return std::nullopt;
    if (platform == RuntimeContentPlatform::MacOS) {
        return executablePath.parent_path().parent_path() / "Resources";
    }
    return executablePath.parent_path();
}
