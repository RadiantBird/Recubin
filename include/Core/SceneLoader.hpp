#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <Instances/Instance.hpp>
#include <yaml-cpp/yaml.h>

/**
 * @brief YAMLファイルからシーン（Instanceツリー）を読み込むクラス
 */
class SceneLoader {
public:
    struct LoadContext {
        std::unordered_map<std::string, std::shared_ptr<Instance>> mergeInstances;

        void registerMergeInstance(const std::string& className,
                                   std::shared_ptr<Instance> instance);
        std::shared_ptr<Instance> findMergeInstance(const std::string& className) const;
    };

    struct SceneDocumentMetadata {
        int version = 0;
        int characterAnimationBindingsVersion = 0;
        // Read-only compatibility data from the retired default_r6_animations
        // header. New saves never emit these fields.
        std::string legacyDefaultR6AnimationDecision;
        std::string legacyWalkContentPath;
        // True when the loaded document had no valid System.ApplicationId and
        // the System constructor supplied a new transient identity.
        bool applicationIdGenerated = false;
    };

    enum class LoadStatus { Success, NotFound, IoError, YamlError, InvalidType,
                            UnsupportedVersion, MissingRoot };
    struct LoadResult {
        std::shared_ptr<Instance> root;
        SceneDocumentMetadata metadata;
        LoadStatus status = LoadStatus::Success;
        std::string message;
        explicit operator bool() const { return status == LoadStatus::Success && root != nullptr; }
    };

    /**
     * @brief 指定されたYAMLファイルからシーンをロードし、ルートオブジェクトを返す
     * @param filePath YAMLファイルのパス
     * @return ロードされたルートオブジェクト（通常はWorkspace）
     */
    static std::shared_ptr<Instance> loadScene(const std::string& filePath);
    static std::shared_ptr<Instance> loadScene(const std::string& filePath,
                                               const LoadContext& context);
    // Result-bearing API used by the editor. loadScene remains a compatibility wrapper.
    static LoadResult loadSceneResult(const std::string& filePath);
    static LoadResult loadSceneResult(const std::string& filePath,
                                      const LoadContext& context);

    static void saveScene(Instance* root, const std::string& filePath);
    static bool saveSceneResult(Instance* root, const std::string& filePath,
                                const SceneDocumentMetadata& metadata = {});
    static void resolveConstraintRefs(Instance* root);

    /**
     * @brief ClassName文字列から適切なInstance派生クラスを生成する
     */
    static std::shared_ptr<Instance> createInstance(const std::string& className);

private:
    /**
     * @brief YAMLノードを再帰的に解析してInstanceを生成する
     */
    static std::shared_ptr<Instance> parseInstance(const YAML::Node& node,
                                                   const LoadContext& context);

    static void saveNode(YAML::Emitter& out, Instance* inst);
};
