#include <Util/EditorLaunchPath.hpp>

bool isMacPortableStudioRoot(const std::filesystem::path& root) {
    std::error_code error;
    if (!std::filesystem::is_directory(root / ".autosave", error) || error) return false;
    if (!std::filesystem::is_directory(root / "shaders", error) || error) return false;
    if (!std::filesystem::is_directory(root / "assets" / "fonts", error) || error) return false;
    return std::filesystem::is_regular_file(root / "RecubinEngine", error) && !error;
}

MacEditorRootSelection selectMacEditorRoot(
    const std::filesystem::path& executablePath,
    const std::filesystem::path& launchWorkingDirectory) {
    const auto executableRoot = executablePath.parent_path().lexically_normal();
    if (!executableRoot.empty() && isMacPortableStudioRoot(executableRoot))
        return {executableRoot, true};
    return {launchWorkingDirectory.lexically_normal(), false};
}
