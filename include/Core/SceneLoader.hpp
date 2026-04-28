#pragma once
#include <memory>
#include <string>
#include <Instances/Instance.hpp>
#include <yaml-cpp/yaml.h>

/**
 * @brief YAMLファイルからシーン（Instanceツリー）を読み込むクラス
 */
class SceneLoader {
public:
    /**
     * @brief 指定されたYAMLファイルからシーンをロードし、ルートオブジェクトを返す
     * @param filePath YAMLファイルのパス
     * @return ロードされたルートオブジェクト（通常はWorkspace）
     */
    static std::shared_ptr<Instance> loadScene(const std::string& filePath);

    static void saveScene(Instance* root, const std::string& filePath);

private:
    /**
     * @brief YAMLノードを再帰的に解析してInstanceを生成する
     */
    static std::shared_ptr<Instance> parseInstance(const YAML::Node& node);

    /**
     * @brief ClassName文字列から適切なInstance派生クラスを生成する
     */
    static std::shared_ptr<Instance> createInstance(const std::string& className);
    static void saveNode(YAML::Emitter& out, Instance* inst);
};
