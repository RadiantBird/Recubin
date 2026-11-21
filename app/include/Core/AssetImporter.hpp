#pragma once
#include <Core/AssetContainer.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Instance;

// .rcaet を読み込んで、シーンに付ける前のInstanceツリーを作る。
//   load        : コンテナを読み、整合性・パス安全性を検証する（ディスクへは何も書かない）。
//                 実行形式が含まれる場合は executables に列挙するので、呼び出し側が確認を取る。
//   instantiate : 埋め込みファイルを展開し、依存パスを書き換えてInstanceを構築する。
// シーンへの追加（名前の一意化・Undo可能なコマンド化）はEditor側が行う。
namespace AssetImporter {

struct ExecutableFile {
    std::string file;            // バンドル内の論理パス
    std::uint64_t size = 0;
    std::string hash;
};

struct Loaded {
    AssetContainer::Document document;
    std::string name;                           // アセット名（YAMLのAsset.Name）
    std::vector<ExecutableFile> executables;    // 埋め込まれた実行形式
};

struct Options {
    bool allowExecutables = false;                  // falseなら実行形式は展開せず、参照を空にする
    std::string extractRoot = "assets/imported";    // 展開先の親ディレクトリ
};

struct Imported {
    std::vector<std::shared_ptr<Instance>> roots;           // 切り離し済み。親へ追加する対象
    std::vector<std::shared_ptr<Instance>> materialsToAdd;  // 取り込み先MaterialServiceへ追加するMaterial
    std::vector<std::string> warnings;
};

struct Result {
    bool success = false;
    std::string message;
    explicit operator bool() const { return success; }
};

Result load(const std::string& path, Loaded& out);

// materialService: 取り込み先のMaterialService（nullptr可）。同名のMaterialがあれば既存を使い、
// 無ければ materialsToAdd に入れる。nullptrの場合はMaterialを追加せず警告する。
Result instantiate(const Loaded& loaded, const Options& options, Instance* materialService,
                   Imported& out);

} // namespace AssetImporter
