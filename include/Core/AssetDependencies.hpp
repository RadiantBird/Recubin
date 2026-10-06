#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <yaml-cpp/yaml.h>

// アセット(.rcaet)が参照する外部ファイル（画像・スクリプト・音・メッシュ・Terrainディレクトリ等）の
// 検出と、取り込み時のパス書換。YAMLのキー名で走査するため、シーン/アセット双方の
// ノードに同じ規則で使える（Packagerのキー集合に、Packagerが拾わないImage等を加えたもの）。
namespace AssetDependencies {

struct Dependency {
    std::string source;          // YAML上に書かれている元の文字列
    bool directory = false;      // Terrain.DataPathのようなディレクトリ依存
    bool exists = false;
    bool absolute = false;       // 絶対パス（プロジェクト外）
    bool executable = false;     // 実行形式（ディレクトリ依存では中に含む場合）
    bool autosave = false;       // エディタの復旧データ領域（埋め込み・同梱の対象外）
    std::uint64_t size = 0;      // バイト数（ディレクトリは合計）
};

struct FileEntry {
    std::string relative;        // 依存内の相対パス（ファイル依存では拡張子付きファイル名）
    std::filesystem::path path;  // 実ファイル
};

// 依存を保持するYAMLキー名（回帰テストでスキーマとの乖離検知に使う）。
const std::vector<std::string_view>& fileKeys();
const std::vector<std::string_view>& sequenceKeys();
const std::vector<std::string_view>& directoryKeys();

// バンドル内の論理パス。相対パスは先頭の "assets/" を除いて保ち、絶対パスや範囲外（".."）は
// "external/<名前>" にする。書き出しと取り込みで同じ規則を使う。
std::string logicalPath(std::string_view source);

// 実行形式として警告する拡張子か（大文字小文字は無視）。
bool isExecutablePath(std::string_view path);

// node配下の全依存を重複なく収集して out へ追加する。
void collect(const YAML::Node& node, std::vector<Dependency>& out);

// dependency の実ファイル一覧。ファイル依存は1件、ディレクトリ依存は再帰列挙（.autosaveは除外）。
std::vector<FileEntry> listFiles(const Dependency& dependency);

// node配下の依存パスを map（元文字列→新文字列）で書き換える。map に無いものは触らない。
void rewrite(YAML::Node node, const std::unordered_map<std::string, std::string>& map);

} // namespace AssetDependencies
