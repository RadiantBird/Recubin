#pragma once

#include <filesystem>

struct MacEditorRootSelection {
    std::filesystem::path root;
    bool portable = false;
};

// Flat Studio distributions are identified by their intentionally-created
// autosave marker plus the resources/binary required by the editor.
bool isMacPortableStudioRoot(const std::filesystem::path& root);
MacEditorRootSelection selectMacEditorRoot(
    const std::filesystem::path& executablePath,
    const std::filesystem::path& launchWorkingDirectory);
