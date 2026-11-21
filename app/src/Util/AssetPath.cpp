#include <Util/AssetPath.hpp>

#include <algorithm>

namespace AssetPath {

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

} // namespace AssetPath
