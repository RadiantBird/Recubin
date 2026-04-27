#include <Core/SceneLoader.hpp>
#include <Core/FileLoader.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/Cube.hpp>
#include <Instances/Script.hpp>
#include <Instances/Model.hpp>
#include <Instances/Decal.hpp>
#include <Instances/Sound.hpp>
#include <Core/AudioService.hpp>
#include <iostream>
#include <memory>

// YAML -> Vector3 変換
namespace YAML {
    template<>
    struct convert<Vector3> {
        static bool decode(const Node& node, Vector3& rhs) {
            if (!node.IsSequence() || node.size() != 3) return false;
            rhs.x = node[0].as<float>();
            rhs.y = node[1].as<float>();
            rhs.z = node[2].as<float>();
            return true;
        }
    };

    template<>
    struct convert<Color4> {
        static bool decode(const Node& node, Color4& rhs) {
            if (!node.IsSequence() || node.size() != 4) return false;
            rhs.r = node[0].as<float>();
            rhs.g = node[1].as<float>();
            rhs.b = node[2].as<float>();
            rhs.a = node[3].as<float>();
            return true;
        }
    };
}

std::shared_ptr<Instance> SceneLoader::loadScene(const std::string& filePath) {
    try {
        YAML::Node config = YAML::LoadFile(filePath);
        if (!config["Root"]) {
            std::cerr << "[SceneLoader] Error: No Root defined in " << filePath << std::endl;
            return nullptr;
        }
        return parseInstance(config["Root"]);
    } catch (const std::exception& e) {
        std::cerr << "[SceneLoader] Exception: " << e.what() << std::endl;
        return nullptr;
    }
}

std::shared_ptr<Instance> SceneLoader::parseInstance(const YAML::Node& node) {
    if (!node["ClassName"]) return nullptr;

    std::string className = node["ClassName"].as<std::string>();
    std::shared_ptr<Instance> instance = createInstance(className);

    if (!instance) {
        if (className == "Sound" && !AudioService::instance) {
            std::cerr << "[SceneLoader] Warning: Failed to create Sound instance because AudioService is not initialized." << std::endl;
        } else {
            std::cerr << "[SceneLoader] Warning: Unknown ClassName: " << className << std::endl;
        }
        return nullptr;
    }

    // 名前
    if (node["Name"]) {
        instance->Name = node["Name"].as<std::string>();
    }

    // プロパティの設定
    if (node["Properties"]) {
        YAML::Node props = node["Properties"];
        for (auto it = props.begin(); it != props.end(); ++it) {
            instance->setProperty(it->first.as<std::string>(), it->second);
        }
    }

    // 子要素の解析
    if (node["Children"]) {
        for (const auto& childNode : node["Children"]) {
            std::shared_ptr<Instance> child = parseInstance(childNode);
            if (child) {
                instance->addChild(child);
            }
        }
    }

    return instance;
}

std::shared_ptr<Instance> SceneLoader::createInstance(const std::string& className) {
    if (className == "Workspace") return std::make_shared<Workspace>();
    if (className == "Cube")      return std::make_shared<Cube>(Vector3(0,0,0), Vector3(1,1,1), 0);
    if (className == "Script")    return std::make_shared<Script>("");
    if (className == "Model")     return std::make_shared<Model>();
    if (className == "Decal")     return std::make_shared<Decal>(0, Face::Front);
    if (className == "Sound") {
        if (AudioService::instance) {
            return std::make_shared<Sound>(*AudioService::instance);
        }
        return nullptr;
    }

    return nullptr;
}
