#include <Util/AssetPath.hpp>

#include <algorithm>

namespace AssetPath {

std::string normalize(std::string_view path) {
    std::string result(path);
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

std::filesystem::path fromStored(std::string_view path) {
    // Scene は UTF-8 で保存する。Windows の path(string) はロケール依存なので、
    // 日本語などを含む ContentPath を正しく wide path に変換できる u8path を使う。
    return std::filesystem::u8path(normalize(path));
}

std::string toStored(const std::filesystem::path& path) {
    const std::u8string value = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

} // namespace AssetPath
