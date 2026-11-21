#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// シーンYAML等に保存されたアセットパスをOS非依存に扱うための変換。
// 読み込み時はWindows/Unix双方の区切りを受け入れ、保存時は'/'へ統一する。
namespace AssetPath {
    std::string normalize(std::string_view path);
    std::filesystem::path fromStored(std::string_view path);
    std::string toStored(const std::filesystem::path& path);
}
