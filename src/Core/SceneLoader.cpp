#include <Core/SceneLoader.hpp>
#include <Core/FileLoader.hpp>
#include <Instances/System.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/Cube.hpp>
#include <Instances/Cylinder.hpp>
#include <Instances/TriangularPrism.hpp>
#include <Instances/Sphere.hpp>
#include <Instances/Script.hpp>
#include <Instances/Model.hpp>
#include <Instances/Decal.hpp>
#include <Instances/Sound.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/Skybox.hpp>
#include <Instances/Rope.hpp>
#include <Instances/Rod.hpp>
#include <Instances/Weld.hpp>
#include <Instances/Motor.hpp>
#include <Core/AudioService.hpp>
#include <iostream>
#include <fstream>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#endif

std::unordered_map<std::string, std::shared_ptr<Instance>> SceneLoader::s_singletons;

void SceneLoader::registerSingleton(const std::string& className, std::shared_ptr<Instance> instance) {
    s_singletons[className] = std::move(instance);
}

void SceneLoader::clearSingletons() {
    s_singletons.clear();
}

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
        std::string yamlContent = FileLoader::readText(filePath);
        if (yamlContent.empty()) return nullptr;
        YAML::Node config = YAML::Load(yamlContent);
        if (!config["Root"]) {
            std::cerr << "[SceneLoader] Error: No Root defined in " << filePath << std::endl;
            return nullptr;
        }
        YAML::Node root = config["Root"];

        // Root が Sequence のとき（旧形式: Root: [{ ClassName: System, ... }]）
        if (root.IsSequence()) {
            auto bag = std::make_shared<Instance>("__root__");
            for (const auto& itemNode : root) {
                std::string cn = itemNode["ClassName"] ? itemNode["ClassName"].as<std::string>() : "";
                auto inst = parseInstance(itemNode);
                if (inst && s_singletons.count(cn) == 0) {
                    bag->addChild(inst);
                }
            }
            resolveConstraintRefs(bag.get());
            for (auto& [n, sing] : s_singletons) resolveConstraintRefs(sing.get());
            return bag;
        }

        // ClassName のない Root は子リストを直接処理する（フラット形式）
        if (!root["ClassName"] && root["Children"]) {
            auto bag = std::make_shared<Instance>("__root__");
            for (const auto& childNode : root["Children"]) {
                std::string cn = childNode["ClassName"] ? childNode["ClassName"].as<std::string>() : "";
                auto inst = parseInstance(childNode);
                // シングルトンはすでに正しい親にいるので addChild で reparent しない
                if (inst && s_singletons.count(cn) == 0) {
                    bag->addChild(inst);
                }
            }
            resolveConstraintRefs(bag.get());
            for (auto& [n, sing] : s_singletons) resolveConstraintRefs(sing.get());
            return bag;
        }

        auto result = parseInstance(root);
        resolveConstraintRefs(result.get());
        for (auto& [n, sing] : s_singletons) resolveConstraintRefs(sing.get());
        return result;
    } catch (const std::exception& e) {
        std::cerr << "[SceneLoader] Exception: " << e.what() << std::endl;
        return nullptr;
    }
}

std::shared_ptr<Instance> SceneLoader::parseInstance(const YAML::Node& node) {
    if (!node["ClassName"]) return nullptr;

    std::string className = node["ClassName"].as<std::string>();

    // シングルトン登録済みなら既存インスタンスへマージ（新規生成しない）
    std::shared_ptr<Instance> instance;
    auto sit = s_singletons.find(className);
    if (sit != s_singletons.end()) {
        instance = sit->second;
    } else {
        instance = createInstance(className);
        if (!instance) {
            if (className == "Sound" && !AudioService::instance) {
                std::cerr << "[SceneLoader] Warning: Failed to create Sound instance because AudioService is not initialized." << std::endl;
            } else {
                std::cerr << "[SceneLoader] Warning: Unknown ClassName: " << className << std::endl;
            }
            return nullptr;
        }
        // 名前はシングルトン以外のみ上書き
        if (node["Name"]) {
            instance->Name = node["Name"].as<std::string>();
        }
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
    if (className == "System")    return std::make_shared<System>();
    if (className == "Workspace") return std::make_shared<Workspace>();
    if (className == "Cube")           return std::make_shared<Cube>(Vector3(0,0,0), Vector3(1,1,1), 0);
    if (className == "Cylinder")       return std::make_shared<Cylinder>(Vector3(0,0,0), Vector3(1,1,1));
    if (className == "TriangularPrism") return std::make_shared<TriangularPrism>(Vector3(0,0,0), Vector3(1,1,1));
    if (className == "Sphere")         return std::make_shared<Sphere>(Vector3(0,0,0), Vector3(1,1,1));
    if (className == "Skybox")         return std::make_shared<Skybox>();
    if (className == "Script")    return std::make_shared<Script>("");
    if (className == "Model")     return std::make_shared<Model>();
    if (className == "Decal")     return std::make_shared<Decal>(0, Face::Front);
    if (className == "Sound") {
        if (AudioService::instance) {
            return std::make_shared<Sound>(*AudioService::instance);
        }
        return nullptr;
    }
    if (className == "Lighting") return std::make_shared<Lighting>();
    if (className == "Instance") return std::make_shared<Instance>("Instance");
    if (className == "Rope")  return std::make_shared<Rope>();
    if (className == "Rod")   return std::make_shared<Rod>();
    if (className == "Weld")  return std::make_shared<Weld>();
    if (className == "Motor") return std::make_shared<Motor>();

    return nullptr;
}

void SceneLoader::resolveConstraintRefs(Instance* node) {
    if (!node) return;

    if (node->IsA("Workspace")) {
        // Workspace の直下の制約インスタンスに対してキューブ名を解決する
        auto resolve = [&](const std::string& cubeName) -> std::shared_ptr<BaseCube> {
            auto* child = node->getChildByPath(cubeName);
            if (child && child->IsA("BaseCube")) {
                return std::static_pointer_cast<BaseCube>(child->shared_from_this());
            }
            return nullptr;
        };

        for (auto& [name, child] : node->children) {
            if (child->IsA("Rope")) {
                auto rope = std::static_pointer_cast<Rope>(child);
                auto c0 = resolve(rope->m_cube0Name);
                auto c1 = resolve(rope->m_cube1Name);
                if (c0 && c1) rope->setCubes(c0, c1);
                else std::cerr << "[SceneLoader] Rope \"" << rope->Name << "\": cube not found\n";
            } else if (child->IsA("Rod")) {
                auto rod = std::static_pointer_cast<Rod>(child);
                auto c0 = resolve(rod->m_cube0Name);
                auto c1 = resolve(rod->m_cube1Name);
                if (c0 && c1) rod->setCubes(c0, c1);
                else std::cerr << "[SceneLoader] Rod \"" << rod->Name << "\": cube not found\n";
            } else if (child->IsA("Weld")) {
                auto weld = std::static_pointer_cast<Weld>(child);
                auto c0 = resolve(weld->m_cube0Name);
                auto c1 = resolve(weld->m_cube1Name);
                if (c0 && c1) weld->setCubes(c0, c1);
                else std::cerr << "[SceneLoader] Weld \"" << weld->Name << "\": cube not found\n";
            } else if (child->IsA("Motor")) {
                auto motor = std::static_pointer_cast<Motor>(child);
                auto c0 = resolve(motor->m_cube0Name);
                auto c1 = resolve(motor->m_cube1Name);
                if (c0 && c1) motor->setCubes(c0, c1);
                else std::cerr << "[SceneLoader] Motor \"" << motor->Name << "\": cube not found\n";
            }
        }
    }

    for (auto& [name, child] : node->children) {
        resolveConstraintRefs(child.get());
    }
}

void SceneLoader::saveNode(YAML::Emitter& out, Instance* inst) {
    out << YAML::BeginMap;
    out << YAML::Key << "ClassName" << YAML::Value << inst->GetClassName();
    out << YAML::Key << "Name"      << YAML::Value << inst->Name;

    // プロパティ
    bool hasProps = inst->IsA("Spatial") || inst->GetClassName() == "Script"
                 || inst->GetClassName() == "Sound" || inst->GetClassName() == "Decal"
                 || inst->GetClassName() == "Lighting" || inst->GetClassName() == "Skybox"
                 || inst->IsA("Rope") || inst->IsA("Rod")
                 || inst->IsA("Weld") || inst->IsA("Motor");
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
            out << YAML::Key << "CastShadow" << YAML::Value << bc->CastShadow;
            out << YAML::Key << "Unlit"      << YAML::Value << bc->Unlit;
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
        if (inst->GetClassName() == "Lighting") {
            const Lighting* lt = static_cast<const Lighting*>(inst);
            out << YAML::Key << "Direction" << YAML::Value
                << YAML::Flow << YAML::BeginSeq
                << lt->lightDir.x << lt->lightDir.y << lt->lightDir.z
                << YAML::EndSeq;
            out << YAML::Key << "Brightness" << YAML::Value << lt->brightness;
        }
        if (inst->GetClassName() == "Skybox") {
            const Skybox* sb = static_cast<const Skybox*>(inst);
            out << YAML::Key << "SkyboxPaths" << YAML::Value
                << YAML::Flow << YAML::BeginSeq
                << sb->skyboxPaths[0] << sb->skyboxPaths[1] << sb->skyboxPaths[2]
                << sb->skyboxPaths[3] << sb->skyboxPaths[4] << sb->skyboxPaths[5]
                << YAML::EndSeq;
        }
        if (inst->GetClassName() == "Sound") {
            const Sound* snd = static_cast<const Sound*>(inst);
            out << YAML::Key << "ContentPath" << YAML::Value << snd->getContentPath();
            out << YAML::Key << "Looped"      << YAML::Value << snd->isLooping();
            out << YAML::Key << "SoundGroup"  << YAML::Value << snd->getSoundGroup();
            out << YAML::Key << "AutoPlay"    << YAML::Value << snd->getAutoPlay();
        }
        if (inst->IsA("Rope")) {
            const Rope* r = static_cast<const Rope*>(inst);
            out << YAML::Key << "Cube0"       << YAML::Value << r->m_cube0Name;
            out << YAML::Key << "Cube1"       << YAML::Value << r->m_cube1Name;
            out << YAML::Key << "MaxDistance" << YAML::Value << r->MaxDistance;
            out << YAML::Key << "Stiffness"   << YAML::Value << r->Stiffness;
            out << YAML::Key << "Damping"     << YAML::Value << r->Damping;
        }
        if (inst->IsA("Rod")) {
            const Rod* r = static_cast<const Rod*>(inst);
            out << YAML::Key << "Cube0" << YAML::Value << r->m_cube0Name;
            out << YAML::Key << "Cube1" << YAML::Value << r->m_cube1Name;
        }
        if (inst->IsA("Weld")) {
            const Weld* w = static_cast<const Weld*>(inst);
            out << YAML::Key << "Cube0" << YAML::Value << w->m_cube0Name;
            out << YAML::Key << "Cube1" << YAML::Value << w->m_cube1Name;
        }
        if (inst->IsA("Motor")) {
            const Motor* m = static_cast<const Motor*>(inst);
            out << YAML::Key << "Cube0" << YAML::Value << m->m_cube0Name;
            out << YAML::Key << "Cube1" << YAML::Value << m->m_cube1Name;
            out << YAML::Key << "Axis"  << YAML::Value
                << YAML::Flow << YAML::BeginSeq
                << m->Axis.x << m->Axis.y << m->Axis.z
                << YAML::EndSeq;
            out << YAML::Key << "DriveVelocity" << YAML::Value << m->DriveVelocity;
            out << YAML::Key << "MaxForce"      << YAML::Value << m->MaxForce;
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
    out << YAML::Key << "Root" << YAML::Value << YAML::BeginMap;
    
    // Root は仮想的な親。その全ての子を Children として保存
    if (!root->children.empty()) {
        out << YAML::Key << "Children" << YAML::Value << YAML::BeginSeq;
        for (auto const& [name, child] : root->children) {
            saveNode(out, child.get());
        }
        out << YAML::EndSeq;
    }
    
    out << YAML::EndMap;
    out << YAML::EndMap;

#ifdef _WIN32
    auto wstrTo = [](const std::string& str) -> std::wstring {
        if (str.empty()) return std::wstring();
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        std::wstring wstrTo(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
        return wstrTo;
    };
    std::ofstream file(wstrTo(filePath));
#else
    std::ofstream file(filePath);
#endif
    
    if (file) file << out.c_str();
    else std::cerr << "[SceneLoader] Failed to open for write: " << filePath << std::endl;
}
