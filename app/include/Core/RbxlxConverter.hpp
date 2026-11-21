#pragma once
#include <filesystem>
#include <string>
#include <vector>

// Roblox の .rbxlx を Recubin のシーン (.rcbn) に、おおまかな互換で変換する。
//   - Part 系は Cube / Sphere / Cylinder / Wedge / Truss / Seat / SpawnLocation に置き換える。
//   - Snap / Weld / WeldConstraint / ManualWeld は Weld、Motor6D は Motor6D にする。
//   - 対応クラスのないサービスやコンテナは、変換できる子孫があれば Folder にする。
//   - rbxassetid などの Roblox 側アセットは取得できないので、参照を空にして警告する。
// Script の Source は「<出力ファイル名>_scripts/」に .luau として書き出す。
namespace RbxlxConverter {

struct Result {
    bool success = false;
    std::string message;
    std::vector<std::string> warnings;   // クラスごとの件数にまとめた警告
    int convertedCount = 0;              // 出力したインスタンス数
    int skippedCount = 0;                // 捨てた Roblox インスタンス数
    explicit operator bool() const { return success; }
};

Result convertFile(const std::filesystem::path& input, const std::filesystem::path& output);

} // namespace RbxlxConverter
