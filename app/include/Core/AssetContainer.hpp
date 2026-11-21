#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <yaml-cpp/yaml.h>

// .rcaet のコンテナ形式（v1）。
//   1行目: "#rcaet 1 yaml=<N>\n"（YAMLコメント。Nは2行目以降のYAMLテキストのバイト数）
//   続き : YAMLテキスト（recubin/Asset/Embedded）
//   末尾 : 埋め込みファイルの生バイトを連結（Embeddedの Offset は末尾領域先頭からの位置）
// 埋め込みが無ければ末尾領域は無く、ファイル全体が通常のYAMLになる。
// Instanceの知識は持たず、バイト列とYAMLだけを扱う。
namespace AssetContainer {

constexpr int FORMAT_VERSION = 1;
constexpr const char* DOCUMENT_TYPE = "asset";

struct EmbeddedFile {
    std::string source;          // YAML内に書かれている元の依存パス文字列
    std::string file;            // バンドル内の論理パス（ディレクトリ依存ではディレクトリ内の相対パス）
    bool directory = false;      // trueなら source はディレクトリ依存で、file はその中の相対パス
    std::string hash;            // "sha256:<hex>"
    std::uint64_t size = 0;
    std::uint64_t offset = 0;
    std::vector<char> data;      // write の入力 / read の結果（検証済み）
};

struct Document {
    YAML::Node yaml;                    // recubin/Asset を含む本体（Embedded キーは除く）
    std::vector<EmbeddedFile> files;    // 全ファイル（ディレクトリ依存は1ファイル1要素）
};

struct Result {
    bool success = false;
    std::string message;
    explicit operator bool() const { return success; }
};

// 論理パスとして安全か（相対・'..'無し・ドライブ文字無し・空要素無し）。
bool isSafeRelativePath(std::string_view path);

// document（recubin/Asset を持つ map）と files から .rcaet を書き出す。
// files の hash/size/offset は書き出し時に確定して書き戻す。一時ファイル経由で置き換える。
Result write(const std::string& path, const YAML::Node& document,
             std::vector<EmbeddedFile>& files);

// .rcaet を読む。ヘッダ・type/version・Embedded の範囲・論理パス・SHA-256を全て検証し、
// 1つでも不正なら失敗を返す（呼び出し側は何も変更しないこと）。
Result read(const std::string& path, Document& out);

} // namespace AssetContainer
