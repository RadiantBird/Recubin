#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// TEMPORARY: store scene asset references and assetRoot as absolute paths while
// relative-path resolution is being stabilized. Set to 0 to restore the
// portable relative-path serialization path without deleting its implementation.
#ifndef RCBN_STORE_ABSOLUTE_ASSET_PATHS
#define RCBN_STORE_ABSOLUTE_ASSET_PATHS 1
#endif

// シーンYAML等に保存されたアセットパスをOS非依存に扱うための変換。
// 読み込み時はWindows/Unix双方の区切りを受け入れ、保存時は'/'へ統一する。
namespace AssetPath {
    std::string normalize(std::string_view path);
    std::filesystem::path fromStored(std::string_view path);
    std::string toStored(const std::filesystem::path& path);

    // Validate the scene assetRoot storage form. Empty is valid for legacy
    // scenes; non-empty values must be portable relative paths, including
    // rejecting Windows drive-relative and rooted forms on every platform.
    bool isPortableRelative(std::string_view stored);

    // Resolve a stored scene path against the scene's asset base directory.
    // Absolute stored paths are returned unchanged for legacy compatibility.
    std::filesystem::path resolveAgainst(std::string_view stored,
                                          const std::filesystem::path& baseDirectory);

    // Resolve legacy references using a user-selected Studio root. The picker
    // may point either at the project directory (paths include `assets/`) or
    // directly at the `assets` directory (paths omit it).
    std::optional<std::filesystem::path> resolveForRepair(
        std::string_view stored, const std::filesystem::path& selectedRoot,
        bool expectDirectory);

    // Return a portable stored path from baseDirectory to target. An empty
    // optional means the two paths have no relative representation (for
    // example, they are on different Windows volumes).
    std::optional<std::string> relativeTo(const std::filesystem::path& target,
                                          const std::filesystem::path& baseDirectory);

    // Resolve the effective asset directory recorded by a scene. Empty
    // assetRoot preserves legacy process-working-directory resolution.
    std::filesystem::path sceneAssetBaseDirectory(std::string_view scenePath,
                                                   std::string_view assetRoot);

    // The active scene base used by consumers that load assets while a scene
    // is being parsed. This is process-global because scene loading and GL
    // resource creation occur on the main thread.
    std::filesystem::path sceneAssetBaseDirectory();
    void setSceneAssetBaseDirectory(const std::filesystem::path& baseDirectory);

    class ScopedSceneAssetBaseDirectory {
    public:
        explicit ScopedSceneAssetBaseDirectory(const std::filesystem::path& baseDirectory);
        ~ScopedSceneAssetBaseDirectory();
        // Keep the currently active base after scope exit instead of
        // restoring the previous one (used after a successful commit).
        void commit();
        ScopedSceneAssetBaseDirectory(const ScopedSceneAssetBaseDirectory&) = delete;
        ScopedSceneAssetBaseDirectory& operator=(const ScopedSceneAssetBaseDirectory&) = delete;
    private:
        std::filesystem::path m_previous;
    };
}
