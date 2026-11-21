#pragma once
#include <Core/AssetContainer.hpp>
#include <Core/AssetDependencies.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <yaml-cpp/yaml.h>

class Instance;

// 選択したInstance部分木を .rcaet として書き出す。
//   prepare: 部分木を複製してサブツリー外の参照を切り、YAMLと依存一覧・警告を作る（元ツリーは無変更）。
//   write  : 選ばれた依存だけを埋め込んで .rcaet へ保存する。
namespace AssetExporter {

struct Plan {
    std::string defaultName;
    YAML::Node document;                                    // recubin + Asset
    std::vector<AssetDependencies::Dependency> dependencies;
    std::vector<std::string> materialNames;                 // 同梱するMaterial
    std::vector<std::string> warnings;                      // 外部参照クリア・スキップ等
    int rootCount = 0;
    explicit operator bool() const { return rootCount > 0; }
};

struct Options {
    std::string name;
    std::vector<std::string> embedSources;                  // 埋め込む依存の source
};

// サービス/シングルトン系は書き出せない。
bool isExportableClass(std::string_view className);
bool isExportable(Instance& instance);

Plan prepare(const std::vector<Instance*>& roots);

AssetContainer::Result write(const Plan& plan, const Options& options, const std::string& path);

} // namespace AssetExporter
