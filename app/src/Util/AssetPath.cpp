#include <Util/AssetPath.hpp>

#include <algorithm>

namespace AssetPath {

namespace {
std::filesystem::path g_sceneAssetBaseDirectory;
}

std::string normalize(std::string_view path) {
    std::string result(path);
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

std::filesystem::path fromStored(std::string_view path) {
    // Scene は UTF-8 で保存する。UTF-8バイト列をu8stringとしてpathへ渡し、
    // Windowsでも日本語などを含むContentPathを正しく扱う。
    const std::string normalized = normalize(path);
    const std::u8string utf8(
        reinterpret_cast<const char8_t*>(normalized.data()), normalized.size());
    return std::filesystem::path(utf8);
}

std::string toStored(const std::filesystem::path& path) {
    const std::u8string value = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

bool isPortableRelative(std::string_view stored) {
    const std::string value = normalize(stored);
    if (value.empty() || value.front() == '/') return false;
    if (value.size() >= 2 && value[1] == ':' &&
        ((value[0] >= 'A' && value[0] <= 'Z') || (value[0] >= 'a' && value[0] <= 'z'))) {
        return false;
    }
    const auto path = fromStored(value);
    return !path.is_absolute() && path.root_name().empty() && path.root_directory().empty();
}

std::filesystem::path resolveAgainst(std::string_view stored,
                                     const std::filesystem::path& baseDirectory) {
    const std::filesystem::path value = fromStored(stored);
    if (value.is_absolute() || baseDirectory.empty()) return value;
    return (baseDirectory / value).lexically_normal();
}

std::optional<std::string> relativeTo(const std::filesystem::path& target,
                                      const std::filesystem::path& baseDirectory) {
    if (target.empty() || baseDirectory.empty()) return std::nullopt;
    std::error_code targetError;
    std::error_code baseError;
    const std::filesystem::path absoluteTarget =
        std::filesystem::absolute(target, targetError).lexically_normal();
    const std::filesystem::path absoluteBase =
        std::filesystem::absolute(baseDirectory, baseError).lexically_normal();
    if (targetError || baseError) return std::nullopt;
    const std::filesystem::path relative = absoluteTarget.lexically_relative(absoluteBase);
    if (relative.empty()) return std::nullopt;
    return toStored(relative);
}

std::filesystem::path sceneAssetBaseDirectory(std::string_view scenePath,
                                               std::string_view assetRoot) {
    if (assetRoot.empty()) return std::filesystem::current_path();
    std::filesystem::path scene = fromStored(scenePath);
    if (scene.empty()) scene = std::filesystem::current_path();
    if (scene.is_relative()) scene = std::filesystem::absolute(scene);
    scene = scene.lexically_normal();
    std::filesystem::path base = scene.parent_path();
    const std::filesystem::path root = fromStored(assetRoot);
    if (!root.empty()) base = root.is_absolute() ? root : base / root;
    return base.lexically_normal();
}

std::filesystem::path sceneAssetBaseDirectory() {
    return g_sceneAssetBaseDirectory;
}

void setSceneAssetBaseDirectory(const std::filesystem::path& baseDirectory) {
    g_sceneAssetBaseDirectory = baseDirectory.lexically_normal();
}

ScopedSceneAssetBaseDirectory::ScopedSceneAssetBaseDirectory(
    const std::filesystem::path& baseDirectory)
    : m_previous(sceneAssetBaseDirectory()) {
    setSceneAssetBaseDirectory(baseDirectory);
}

ScopedSceneAssetBaseDirectory::~ScopedSceneAssetBaseDirectory() {
    setSceneAssetBaseDirectory(m_previous);
}

void ScopedSceneAssetBaseDirectory::commit() {
    m_previous = sceneAssetBaseDirectory();
}

} // namespace AssetPath
