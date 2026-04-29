#include <Core/SceneLoader.hpp>
#include <Core/FileLoader.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/Cube.hpp>
#include <Instances/Cylinder.hpp>
#include <Instances/TriangularPrism.hpp>
#include <Instances/Sphere.hpp>
#include <Instances/Script.hpp>
#include <Instances/Model.hpp>
#include <Instances/Decal.hpp>
#include <Instances/Sound.hpp>
#include <Core/AudioService.hpp>
#include <iostream>
#include <fstream>
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
    if (className == "Cube")           return std::make_shared<Cube>(Vector3(0,0,0), Vector3(1,1,1), 0);
    if (className == "Cylinder")       return std::make_shared<Cylinder>(Vector3(0,0,0), Vector3(1,1,1));
    if (className == "TriangularPrism") return std::make_shared<TriangularPrism>(Vector3(0,0,0), Vector3(1,1,1));
    if (className == "Sphere")         return std::make_shared<Sphere>(Vector3(0,0,0), Vector3(1,1,1));
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

void SceneLoader::saveNode(YAML::Emitter& out, Instance* inst) {
    out << YAML::BeginMap;
    out << YAML::Key << "ClassName" << YAML::Value << inst->GetClassName();
    out << YAML::Key << "Name"      << YAML::Value << inst->Name;

    // プロパティ
    bool hasProps = inst->IsA("Spatial") || inst->GetClassName() == "Script"
                 || inst->GetClassName() == "Sound" || inst->GetClassName() == "Decal";
    if (hasProps) {
        out << YAML::Key << "Properties" << YAML::Value << YAML::BeginMap;

        if (inst->IsA("Spatial")) {
            const Spatial* s = static_cast<const Spatial*>(inst);
            out << YAML::Key << "Position" << YAML::Value
                << YAML::Flow << YAML::BeginSeq
                << s->Position.x << s->Position.y << s->Position.z
                << YAML::EndSeq;
            out << YAML::Key << "Size" << YAML::Value
                << YAML::Flow << YAML::BeginSeq
                << s->Size.x << s->Size.y << s->Size.z
                << YAML::EndSeq;
            out << YAML::Key << "Rotation" << YAML::Value
                << YAML::Flow << YAML::BeginSeq
                << s->cframe.Rotation.x << s->cframe.Rotation.y
                << s->cframe.Rotation.z << s->cframe.Rotation.w
                << YAML::EndSeq;
        }
        if (inst->IsA("BaseCube")) {
            const BaseCube* bc = static_cast<const BaseCube*>(inst);
            out << YAML::Key << "Color" << YAML::Value
                << YAML::Flow << YAML::BeginSeq
                << bc->Color.r << bc->Color.g << bc->Color.b << bc->Color.a
                << YAML::EndSeq;
            out << YAML::Key << "Anchored"   << YAML::Value << bc->Anchored;
            out << YAML::Key << "CanCollide" << YAML::Value << bc->CanCollide;
        }
        if (inst->GetClassName() == "Script") {
            const Script* sc = static_cast<const Script*>(inst);
            out << YAML::Key << "ContentPath" << YAML::Value << sc->Path;
        }
        if (inst->GetClassName() == "Decal") {
            const Decal* d = static_cast<const Decal*>(inst);
            out << YAML::Key << "Face"    << YAML::Value << static_cast<int>(d->face);
            if (!d->texturePath.empty())
                out << YAML::Key << "Texture" << YAML::Value << d->texturePath;
        }
        if (inst->GetClassName() == "Sound") {
            const Sound* snd = static_cast<const Sound*>(inst);
            out << YAML::Key << "ContentPath" << YAML::Value << snd->getContentPath();
            out << YAML::Key << "Looped"      << YAML::Value << snd->isLooping();
            out << YAML::Key << "SoundGroup"  << YAML::Value << snd->getSoundGroup();
            out << YAML::Key << "AutoPlay"    << YAML::Value << snd->getAutoPlay();
        }

        out << YAML::EndMap;
    }

    // 子要素
    if (!inst->children.empty()) {
        out << YAML::Key << "Children" << YAML::Value << YAML::BeginSeq;
        for (auto const& [name, child] : inst->children) {
            saveNode(out, child.get());
        }
        out << YAML::EndSeq;
    }

    out << YAML::EndMap;
}

void SceneLoader::saveScene(Instance* root, const std::string& filePath) {
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "Root" << YAML::Value;
    saveNode(out, root);
    out << YAML::EndMap;

    std::ofstream file(filePath);
    if (file) file << out.c_str();
    else std::cerr << "[SceneLoader] Failed to open for write: " << filePath << std::endl;
}
