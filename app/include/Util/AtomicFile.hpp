#pragma once
#include <filesystem>
#include <string>
#include <string_view>

// 一時ファイルへ書き込んでから rename で置き換える。失敗しても元ファイルは変更されない。
namespace AtomicFile {
    bool writeReplacing(const std::filesystem::path& target, std::string_view data, std::string& error);
}
