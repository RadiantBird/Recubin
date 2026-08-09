#include <Util/AssetPath.hpp>

#include <algorithm>

namespace AssetPath {

std::string normalize(std::string_view path) {
    std::string result(path);
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

std::filesystem::path fromStored(std::string_view path) {
    return std::filesystem::path(normalize(path));
}

std::string toStored(const std::filesystem::path& path) {
    return path.generic_string();
}

} // namespace AssetPath
