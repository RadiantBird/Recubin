#include <Core/SceneLoader.hpp>
#include <Core/FileLoader.hpp>
#include <Instances/System.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/Cube.hpp>
#include <Instances/Cylinder.hpp>
#include <Instances/TriangularPrism.hpp>
#include <Instances/Truss.hpp>
#include <Instances/Seat.hpp>
#include <Instances/Sphere.hpp>
#include <Instances/MeshCube.hpp>
#include <Instances/LiquidCube.hpp>
#include <Instances/Script.hpp>
#include <Instances/Model.hpp>
#include <Instances/Decal.hpp>
#include <Instances/Texture.hpp>
#include <Instances/Canvas.hpp>
#include <Instances/Highlight.hpp>
#include <Instances/Sound.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/PointLight.hpp>
#include <Instances/SpotLight.hpp>
#include <Instances/PostEffect.hpp>
#include <Instances/AppImage.hpp>
#include <Instances/FileRef.hpp>
#include <Instances/SignalEvent.hpp>
#include <Instances/Humanoid.hpp>
#include <Instances/PathfindingService.hpp>
#include <Core/PropertyRegistry.hpp>
#include <Instances/Animation.hpp>
#include <Instances/StarterCharacter.hpp>
#include <Instances/Skybox.hpp>
#include <Instances/Sun.hpp>
#include <Instances/Moon.hpp>
#include <include/Core/Terrain.hpp>
#include <Instances/Rope.hpp>
#include <Instances/Rod.hpp>
#include <Instances/Weld.hpp>
#include <Instances/Motor.hpp>
#include <Instances/BallSocket.hpp>
#include <Instances/NoCollision.hpp>
#include <Instances/ValueBase.hpp>
#include <Instances/IntValue.hpp>
#include <Instances/BoolValue.hpp>
#include <Instances/NumberValue.hpp>
#include <Instances/Vector3Value.hpp>
#include <Instances/Color4Value.hpp>
#include <Instances/CFrameValue.hpp>
#include <Instances/QuaternionValue.hpp>
#include <Instances/ObjectValue.hpp>
#include <Instances/Attachment.hpp>
#include <Instances/Force.hpp>
#include <Instances/TextLabel.hpp>
#include <Instances/TextButton.hpp>
#include <Instances/ImageLabel.hpp>
#include <Instances/ImageButton.hpp>
#include <Instances/SurfaceGui.hpp>
#include <Instances/BillboardGui.hpp>
#include <Instances/ProximityPrompt.hpp>
#include <Instances/Folder.hpp>
#include <Instances/Tool.hpp>
#include <Instances/Users.hpp>
#include <Instances/LocalScript.hpp>
#include <Instances/ModuleScript.hpp>
#include <Instances/ParticleEmitter.hpp>
#include <Instances/Weather.hpp>
#include <Core/User.hpp>
#include <Core/AudioService.hpp>
#include <Util/Logger.hpp>
#include <iostream>
#include <fstream>
#include <memory>

#ifdef _WIN32
#include <windows26.h>
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

        // 非シングルトンの root 直下インスタンスを受け取るコンテナを決定する。
        // System シングルトンがある場合はそこへ直接追加し、ない場合は bag を返す。
        auto getOrphanParent = [&]() -> std::shared_ptr<Instance> {
            auto it = s_singletons.find("System");
            if (it != s_singletons.end()) return it->second;
            return std::make_shared<Instance>("__root__");
        };

        // Root が Sequence のとき（旧形式: Root: [{ ClassName: System, ... }]）
        if (root.IsSequence()) {
            auto bag = getOrphanParent();
            for (const auto& itemNode : root) {
                std::string cn = itemNode["ClassName"] ? itemNode["ClassName"].as<std::string>() : "";
                auto inst = parseInstance(itemNode);
                if (inst && s_singletons.count(cn) == 0) {
                    bag->addChild(inst);
                }
            }
            resolveConstraintRefs(bag.get());
            // bag が System シングルトン自身の場合、同じツリーを二重解決して警告も二重に出るためスキップ
            for (auto& [n, sing] : s_singletons)
                if (sing != bag) resolveConstraintRefs(sing.get());
            return bag;
        }

        // ClassName のない Root は子リストを直接処理する（フラット形式）
        if (!root["ClassName"] && root["Children"]) {
            auto bag = getOrphanParent();
            if (root["Properties"]) {
                YAML::Node props = root["Properties"];
                for (auto it = props.begin(); it != props.end(); ++it) {
                    bag->setProperty(it->first.as<std::string>(), it->second);
                }
            }
            for (const auto& childNode : root["Children"]) {
                std::string cn = childNode["ClassName"] ? childNode["ClassName"].as<std::string>() : "";
                auto inst = parseInstance(childNode);
                // シングルトンはすでに正しい親にいるので addChild で reparent しない
                if (inst && s_singletons.count(cn) == 0) {
                    bag->addChild(inst);
                }
            }
            resolveConstraintRefs(bag.get());
            // bag が System シングルトン自身の場合、同じツリーを二重解決して警告も二重に出るためスキップ
            for (auto& [n, sing] : s_singletons)
                if (sing != bag) resolveConstraintRefs(sing.get());
            return bag;
        }

        auto result = parseInstance(root);
        resolveConstraintRefs(result.get());
        for (auto& [n, sing] : s_singletons)
            if (sing != result) resolveConstraintRefs(sing.get());
        return result;
    } catch (const std::exception& e) {
        std::cerr << "[SceneLoader] Exception: " << e.what() << std::endl;
        return nullptr;
    }
}

std::shared_ptr<Instance> SceneLoader::parseInstance(const YAML::Node& node) {
    if (!node["ClassName"]) {
        RCBN_WARN("[SceneLoader] Instance node is missing ClassName — skipped");
        return nullptr;
    }

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
                RCBN_WARN("[SceneLoader] Sound skipped: AudioService not initialized");
            } else {
                RCBN_WARN("[SceneLoader] Unknown ClassName '" + className + "' — instance skipped");
                std::cerr << "[SceneLoader] WARN: Unknown ClassName '" << className << "'\n";
            }
            return nullptr;
        }
    }

    // プロパティの設定
    if (node["Properties"]) {
        YAML::Node props = node["Properties"];
        for (auto it = props.begin(); it != props.end(); ++it) {
            instance->setProperty(it->first.as<std::string>(), it->second);
        }
    }

    // 名前はシングルトン以外のみ、かつ「プロパティ適用の後」に上書きする。
    // Decal/Texture の setFace() 等は setProperty 内で Name を書き換える（既定名へ）ため、
    // ここで保存名を最終的に当て直さないとユーザー指定名が潰れ、名前参照が壊れる。
    if (sit == s_singletons.end() && node["Name"]) {
        instance->Name = node["Name"].as<std::string>();
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
    if (className == "PathfindingService") return std::make_shared<PathfindingService>();
    if (className == "Cube")           return std::make_shared<Cube>(Vector3(0,0,0), Vector3(1,1,1), 0);
    if (className == "Cylinder")       return std::make_shared<Cylinder>(Vector3(0,0,0), Vector3(1,1,1));
    if (className == "TriangularPrism") return std::make_shared<TriangularPrism>(Vector3(0,0,0), Vector3(1,1,1));
    if (className == "Truss")          return std::make_shared<Truss>(Vector3(0,0,0), Vector3(1,1,1), 0);
    if (className == "Seat")           return std::make_shared<Seat>(Vector3(0,0,0), Vector3(1,1,1), 0);
    if (className == "Sphere")         return std::make_shared<Sphere>(Vector3(0,0,0), Vector3(1,1,1));
    if (className == "MeshCube")       return std::make_shared<MeshCube>(Vector3(0,0,0), Vector3(1,1,1));
    if (className == "LiquidCube")      return std::make_shared<LiquidCube>(Vector3(0,0,0), Vector3(4,2,4));
    if (className == "Skybox")         return std::make_shared<Skybox>();
    if (className == "Sun")            return std::make_shared<Sun>();
    if (className == "Moon")           return std::make_shared<Moon>();
    if (className == "Script")    return std::make_shared<Script>("");
    if (className == "Model")     return std::make_shared<Model>();
    if (className == "Decal")     return std::make_shared<Decal>(0, Face::Front);
    if (className == "Texture")   return std::make_shared<Texture>(0, Face::Front);
    if (className == "Canvas")    return std::make_shared<Canvas>();
    if (className == "Highlight") return std::make_shared<Highlight>();
    if (className == "SignalEvent") return std::make_shared<SignalEvent>();
    if (className == "Sound") {
        if (AudioService::instance) {
            return std::make_shared<Sound>(*AudioService::instance);
        }
        return nullptr;
    }
    if (className == "Lighting")  return std::make_shared<Lighting>();
    if (className == "PointLight") return std::make_shared<PointLight>();
    if (className == "SpotLight")  return std::make_shared<SpotLight>();
    if (className == "PostEffect") return std::make_shared<PostEffect>();
    if (className == "AppImage")         return std::make_shared<AppImage>();
    if (className == "FileRef")          return std::make_shared<FileRef>();
    if (className == "Humanoid")          return std::make_shared<Humanoid>();
    if (className == "Animation")         return std::make_shared<Animation>();
    if (className == "StarterCharacter")  return std::make_shared<StarterCharacter>();
    if (className == "Terrain") return std::make_shared<Terrain>();
    if (className == "Instance") return std::make_shared<Instance>("Instance");
    if (className == "Rope")  return std::make_shared<Rope>();
    if (className == "Rod")   return std::make_shared<Rod>();
    if (className == "BallSocket") return std::make_shared<BallSocket>();
    if (className == "NoCollision") return std::make_shared<NoCollision>();
    if (className == "IntValue")        return std::make_shared<IntValue>();
    if (className == "BoolValue")       return std::make_shared<BoolValue>();
    if (className == "NumberValue")     return std::make_shared<NumberValue>();
    if (className == "Vector3Value")    return std::make_shared<Vector3Value>();
    if (className == "Color4Value")     return std::make_shared<Color4Value>();
    if (className == "CFrameValue")     return std::make_shared<CFrameValue>();
    if (className == "QuaternionValue") return std::make_shared<QuaternionValue>();
    if (className == "ObjectValue")     return std::make_shared<ObjectValue>();
    if (className == "Weld")  return std::make_shared<Weld>();
    if (className == "Motor")        return std::make_shared<Motor>();
    if (className == "Attachment")   return std::make_shared<Attachment>();
    if (className == "Force")        return std::make_shared<Force>();
    if (className == "TextLabel")    return std::make_shared<TextLabel>();
    if (className == "TextButton")   return std::make_shared<TextButton>();
    if (className == "ImageLabel")   return std::make_shared<ImageLabel>();
    if (className == "ImageButton")  return std::make_shared<ImageButton>();
    if (className == "SurfaceGui")   return std::make_shared<SurfaceGui>();
    if (className == "BillboardGui") return std::make_shared<BillboardGui>();
    if (className == "ProximityPrompt") return std::make_shared<ProximityPrompt>();
    if (className == "Folder")   return std::make_shared<Folder>();
    if (className == "Users")    return std::make_shared<Users>();
    if (className == "LocalScript") return std::make_shared<LocalScript>("");
    if (className == "ModuleScript") return std::make_shared<ModuleScript>("");
    if (className == "Tool")     return std::make_shared<Tool>("Tool");
    if (className == "ParticleEmitter") return std::make_shared<ParticleEmitter>();
    if (className == "Weather") return std::make_shared<Weather>();

    return nullptr;
}

void SceneLoader::resolveConstraintRefs(Instance* node) {
    if (!node) return;
    Instance* sceneRoot = node;  // ルート相対パス（"StarterCharacter\\Head" 等）の解決基点

    // 制約のキューブ名を解決する。まずその制約の最寄り Workspace を基点に（Workspace 相対
    // パス用）、見つからなければ制約自身の最上位祖先を基点に解決する。
    // getWorkspaceRelativePath() も Workspace 外では最上位祖先相対で保存するため、
    // resolveConstraintRefs(User) のように走査起点が途中のノードでも同じ規約になる。
    auto resolveFor = [&](Instance* constraint, const std::string& cubeName) -> std::shared_ptr<BaseCube> {
        Instance* found = nullptr;
        if (Instance* ws = constraint->findFirstAncestorWorkspace())
            found = ws->getChildByPath(cubeName);
        if (!(found && found->IsA("BaseCube"))) {
            Instance* top = constraint;
            for (auto p = constraint->Parent.lock(); p; p = p->Parent.lock())
                top = p.get();
            found = top->getChildByPath(cubeName);
        }
        // 親接続前の部分ツリーや旧形式の相対パス向けフォールバック。
        if (!(found && found->IsA("BaseCube")) && sceneRoot != constraint)
            found = sceneRoot->getChildByPath(cubeName);
        if (found && found->IsA("BaseCube"))
            return std::static_pointer_cast<BaseCube>(found->shared_from_this());
        return nullptr;
    };

    // 名前が空の参照は「未設定」（エディターで挿入直後など）の正当な状態なので解決も警告もしない。
    // 名前が設定されているのに解決できなかった場合のみ、どの参照が失敗したかをフルパス付きで警告する。
    auto warnUnresolved = [](Instance* constraint, const char* cls,
                             bool miss0, const std::string& n0,
                             bool miss1, const std::string& n1) {
        std::cerr << "[SceneLoader] " << cls << " \"" << constraint->getFullPath()
                  << "\": cube not found (";
        if (miss0)          std::cerr << "Cube0=\"" << n0 << "\"";
        if (miss0 && miss1) std::cerr << ", ";
        if (miss1)          std::cerr << "Cube1=\"" << n1 << "\"";
        std::cerr << ")\n";
    };

    // 各制約クラス共通の解決処理（cube0Name/cube1Name → resolveFor → setCubes/警告）
    auto resolvePair = [&](Instance* c, const char* cls,
                           const std::string& n0, const std::string& n1,
                           auto&& applyCubes) {
        bool cfg0 = !n0.empty(), cfg1 = !n1.empty();
        auto c0 = cfg0 ? resolveFor(c, n0) : nullptr;
        auto c1 = cfg1 ? resolveFor(c, n1) : nullptr;
        if (c0 && c1) applyCubes(c0, c1);
        bool miss0 = cfg0 && !c0, miss1 = cfg1 && !c1;
        if (miss0 || miss1) warnUnresolved(c, cls, miss0, n0, miss1, n1);
    };

    auto walk = [&](auto& self, Instance* inst) -> void {
        for (auto& [name, child] : inst->children) {
            Instance* c = child.get();
            if (child->IsA("Rope")) {
                auto rope = std::static_pointer_cast<Rope>(child);
                resolvePair(c, "Rope", rope->m_cube0Name, rope->m_cube1Name,
                            [&](auto c0, auto c1) { rope->setCubes(c0, c1); rope->resolveAttachments(); });
            } else if (child->IsA("Rod")) {
                auto rod = std::static_pointer_cast<Rod>(child);
                resolvePair(c, "Rod", rod->m_cube0Name, rod->m_cube1Name,
                            [&](auto c0, auto c1) { rod->setCubes(c0, c1); rod->resolveAttachments(); });
            } else if (child->IsA("BallSocket")) {
                auto bs = std::static_pointer_cast<BallSocket>(child);
                resolvePair(c, "BallSocket", bs->m_cube0Name, bs->m_cube1Name,
                            [&](auto c0, auto c1) { bs->setCubes(c0, c1); bs->resolveAttachments(); });
            } else if (child->IsA("NoCollision")) {
                auto nc = std::static_pointer_cast<NoCollision>(child);
                resolvePair(c, "NoCollision", nc->m_cube0Name, nc->m_cube1Name,
                            [&](auto c0, auto c1) { nc->setCubes(c0, c1); });
            } else if (child->IsA("Weld")) {
                auto weld = std::static_pointer_cast<Weld>(child);
                resolvePair(c, "Weld", weld->m_cube0Name, weld->m_cube1Name,
                            [&](auto c0, auto c1) { weld->setCubes(c0, c1); });
            } else if (child->IsA("Motor")) {
                auto motor = std::static_pointer_cast<Motor>(child);
                resolvePair(c, "Motor", motor->m_cube0Name, motor->m_cube1Name,
                            [&](auto c0, auto c1) { motor->setCubes(c0, c1); motor->resolveAttachments(); });
            } else if (child->IsA("ObjectValue")) {
                auto ov = std::static_pointer_cast<ObjectValue>(child);
                if (!ov->m_targetPathName.empty()) {
                    Instance* top = ov.get();
                    for (auto p = ov->Parent.lock(); p; p = p->Parent.lock())
                        top = p.get();
                    Instance* found = top->getChildByPath(ov->m_targetPathName);
                    if (!found && top != sceneRoot)
                        found = sceneRoot->getChildByPath(ov->m_targetPathName);
                    if (found)
                        ov->resolveTarget(found->shared_from_this());
                    else
                        std::cerr << "[SceneLoader] ObjectValue \"" << ov->getFullPath()
                                  << "\": target not found (Value=\"" << ov->m_targetPathName << "\")\n";
                }
            }
            self(self, c);
        }
    };
    walk(walk, node);
}

void SceneLoader::saveNode(YAML::Emitter& out, Instance* inst) {
    out << YAML::BeginMap;
    out << YAML::Key << "ClassName" << YAML::Value << inst->getClassName();
    out << YAML::Key << "Name"      << YAML::Value << inst->Name;

    // プロパティ
    bool hasProps = inst->IsA("Spatial") || inst->IsA("Script")
                 || inst->getClassName() == "Sound" || inst->getClassName() == "Decal"
                 || inst->getClassName() == "Texture"
                 || inst->getClassName() == "Lighting" || inst->getClassName() == "Skybox"
                 || inst->IsA("LightSource")
                 || inst->getClassName() == "PostEffect"
                 || inst->getClassName() == "AppImage"
                 || inst->getClassName() == "FileRef"
                 || inst->getClassName() == "Humanoid"
                 || inst->getClassName() == "Animation"
                 || inst->IsA("Rope") || inst->IsA("Rod") || inst->IsA("BallSocket")
                 || inst->IsA("NoCollision")
                 || inst->IsA("Weld") || inst->IsA("Motor")
                 || inst->getClassName() == "Force"
                 || inst->IsA("ScreenGuiObject")
                 || inst->IsA("WorldGuiObject")
                 || inst->getClassName() == "ProximityPrompt"
                 || inst->getClassName() == "Terrain"
                 || inst->getClassName() == "User"
                 || inst->getClassName() == "Tool"
                 || inst->getClassName() == "System"
                 || inst->getClassName() == "ParticleEmitter"
                 || inst->getClassName() == "Weather"
                 || inst->getClassName() == "Canvas"
                 || inst->getClassName() == "Highlight"
                 || inst->IsA("ValueBase")
                 || inst->IsA("Workspace"); // NOTE: プロパティを最近追加した

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
            out << YAML::Key << "MassDensity" << YAML::Value << bc->MassDensity;
            out << YAML::Key << "MaterialType"    << YAML::Value << static_cast<int>(bc->material.type);
            out << YAML::Key << "StaticFriction"  << YAML::Value << bc->material.staticFriction;
            out << YAML::Key << "DynamicFriction" << YAML::Value << bc->material.dynamicFriction;
            out << YAML::Key << "Restitution"     << YAML::Value << bc->material.restitution;
        }
        if (inst->getClassName() == "LiquidCube") {
            PropertyRegistry::saveProperties(out, inst, "LiquidCube");  // Density
        }
        if (inst->getClassName() == "Sun") {
            PropertyRegistry::saveProperties(out, inst, "Sun");  // Angle
        }
        if (inst->IsA("Script")) {
            const Script* sc = static_cast<const Script*>(inst);
            out << YAML::Key << "ContentPath" << YAML::Value << sc->Path;
            out << YAML::Key << "Enabled"     << YAML::Value << sc->Enabled;
        }
        if (inst->getClassName() == "Decal") {
            const Decal* d = static_cast<const Decal*>(inst);
            out << YAML::Key << "Face"    << YAML::Value << static_cast<int>(d->face);
            if (!d->texturePath.empty())
                out << YAML::Key << "Texture" << YAML::Value << d->texturePath;
            out << YAML::Key << "UVCenter" << YAML::Value
                << YAML::Flow << YAML::BeginSeq
                << d->UVCenter.x << d->UVCenter.y
                << YAML::EndSeq;
            out << YAML::Key << "UVRadius" << YAML::Value << d->UVRadius;
            out << YAML::Key << "Mode" << YAML::Value << static_cast<int>(d->Mode);
        }
        if (inst->getClassName() == "Texture") {
            const Texture* tx = static_cast<const Texture*>(inst);
            out << YAML::Key << "Face" << YAML::Value << static_cast<int>(tx->face);
            out << YAML::Key << "Color" << YAML::Value
                << YAML::Flow << YAML::BeginSeq
                << tx->Color.r << tx->Color.g << tx->Color.b << tx->Color.a
                << YAML::EndSeq;
            out << YAML::Key << "StudsPerTileU" << YAML::Value << tx->StudsPerTileU;
            out << YAML::Key << "StudsPerTileV" << YAML::Value << tx->StudsPerTileV;
            if (!tx->texturePath.empty())
                out << YAML::Key << "Texture" << YAML::Value << tx->texturePath;
        }
        if (inst->getClassName() == "AppImage") {
            PropertyRegistry::saveProperties(out, inst, "AppImage");
        }
        if (inst->getClassName() == "FileRef") {
            PropertyRegistry::saveProperties(out, inst, "FileRef");  // ContentPath（空なら省略）
        }
        if (inst->getClassName() == "MeshCube") {
            const MeshCube* mc = static_cast<const MeshCube*>(inst);
            if (!mc->MeshFile.empty())
                out << YAML::Key << "MeshFile" << YAML::Value << mc->MeshFile;
        }
        if (inst->getClassName() == "Terrain") {
            const Terrain* tr = static_cast<const Terrain*>(inst);
            out << YAML::Key << "Enabled"  << YAML::Value << tr->Enabled;
            out << YAML::Key << "DataPath" << YAML::Value << tr->DataPath;
            out << YAML::Key << "Seed"     << YAML::Value << tr->Seed;
            out << YAML::Key << "Flat"     << YAML::Value << tr->Flat;
        }
        if (inst->getClassName() == "Humanoid") {
            // プロパティは PropertyRegistry の表から出力（WalkSpeed/JumpPower/
            // MaxHealth/RespawnTime/Health をまとめて保存）
            PropertyRegistry::saveProperties(out, inst, "Humanoid");
        }
        if (inst->getClassName() == "Animation") {
            const Animation* anim = static_cast<const Animation*>(inst);
            out << YAML::Key << "Length" << YAML::Value << anim->Length;
            out << YAML::Key << "Speed"  << YAML::Value << anim->Speed;
            out << YAML::Key << "Looped" << YAML::Value << anim->Looped;
            out << YAML::Key << "Tracks" << YAML::Value << YAML::BeginSeq;
            for (const AnimTrack& tr : anim->getTracks()) {
                out << YAML::BeginMap;
                out << YAML::Key << "PartName" << YAML::Value << tr.partName;
                out << YAML::Key << "Keyframes" << YAML::Value << YAML::BeginSeq;
                for (const Keyframe& kf : tr.keyframes) {
                    out << YAML::BeginMap;
                    out << YAML::Key << "Time" << YAML::Value << kf.time;
                    out << YAML::Key << "Position" << YAML::Value
                        << YAML::Flow << YAML::BeginSeq
                        << kf.cframe.Position.x << kf.cframe.Position.y << kf.cframe.Position.z
                        << YAML::EndSeq;
                    out << YAML::Key << "Rotation" << YAML::Value
                        << YAML::Flow << YAML::BeginSeq
                        << kf.cframe.Rotation.x << kf.cframe.Rotation.y
                        << kf.cframe.Rotation.z << kf.cframe.Rotation.w
                        << YAML::EndSeq;
                    out << YAML::Key << "Easing" << YAML::Value << static_cast<int>(kf.easing);
                    out << YAML::EndMap;
                }
                out << YAML::EndSeq;
                out << YAML::EndMap;
            }
            out << YAML::EndSeq;
        }
        if (inst->getClassName() == "Lighting") {
            PropertyRegistry::saveProperties(out, inst, "Lighting");
        }
        if (inst->IsA("LightSource")) {
            // 基底走査 save: LightSource(Color/Brightness/Range) + 派生分(SpotLight.Angle等)を最派生名で一括出力
            PropertyRegistry::saveProperties(out, inst, inst->getClassName());
        }
        if (inst->getClassName() == "ParticleEmitter") {
            PropertyRegistry::saveProperties(out, inst, "ParticleEmitter");
        }
        if (inst->getClassName() == "Weather") {
            PropertyRegistry::saveProperties(out, inst, "Weather");
        }
        if (inst->getClassName() == "PostEffect") {
            const PostEffect* pe = static_cast<const PostEffect*>(inst);
            out << YAML::Key << "Enabled"   << YAML::Value << pe->Enabled;
            out << YAML::Key << "Type"      << YAML::Value << static_cast<int>(pe->Type);
            out << YAML::Key << "ZIndex"    << YAML::Value << pe->ZIndex;
            out << YAML::Key << "Intensity" << YAML::Value << pe->Intensity;
            out << YAML::Key << "Param1"    << YAML::Value << pe->Param1;
            out << YAML::Key << "Param2"    << YAML::Value << pe->Param2;
        }
        if (inst->getClassName() == "Skybox") {
            const Skybox* sb = static_cast<const Skybox*>(inst);
            out << YAML::Key << "SkyboxPaths" << YAML::Value
                << YAML::Flow << YAML::BeginSeq
                << sb->skyboxPaths[0] << sb->skyboxPaths[1] << sb->skyboxPaths[2]
                << sb->skyboxPaths[3] << sb->skyboxPaths[4] << sb->skyboxPaths[5]
                << YAML::EndSeq;
        }
        if (inst->getClassName() == "Sound") {
            const Sound* snd = static_cast<const Sound*>(inst);
            out << YAML::Key << "ContentPath"   << YAML::Value << snd->getContentPath();
            out << YAML::Key << "Looped"        << YAML::Value << snd->isLooping();
            out << YAML::Key << "SoundGroup"    << YAML::Value << snd->getSoundGroup();
            out << YAML::Key << "AutoPlay"      << YAML::Value << snd->getAutoPlay();
            out << YAML::Key << "Volume"        << YAML::Value << snd->getVolume();
            out << YAML::Key << "Speed"         << YAML::Value << snd->getSpeed();
            out << YAML::Key << "PreservePitch" << YAML::Value << snd->getPreservePitch();
        }
        if (inst->IsA("Rope")) {
            Rope* r = static_cast<Rope*>(inst);
            r->refreshRefNames();
            out << YAML::Key << "Cube0"       << YAML::Value << r->m_cube0Name;
            out << YAML::Key << "Cube1"       << YAML::Value << r->m_cube1Name;
            if (!r->m_attachment0Name.empty()) out << YAML::Key << "Attachment0" << YAML::Value << r->m_attachment0Name;
            if (!r->m_attachment1Name.empty()) out << YAML::Key << "Attachment1" << YAML::Value << r->m_attachment1Name;
            out << YAML::Key << "MaxDistance" << YAML::Value << r->MaxDistance;
            out << YAML::Key << "Stiffness"   << YAML::Value << r->Stiffness;
            out << YAML::Key << "Damping"     << YAML::Value << r->Damping;
            out << YAML::Key << "Color"     << YAML::Value << YAML::Flow << YAML::BeginSeq << r->Color.r << r->Color.g << r->Color.b << r->Color.a << YAML::EndSeq;
            out << YAML::Key << "LineWidth" << YAML::Value << r->LineWidth;
        }
        if (inst->IsA("Rod")) {
            Rod* r = static_cast<Rod*>(inst);
            r->refreshRefNames();
            out << YAML::Key << "Cube0" << YAML::Value << r->m_cube0Name;
            out << YAML::Key << "Cube1" << YAML::Value << r->m_cube1Name;
            if (!r->m_attachment0Name.empty()) out << YAML::Key << "Attachment0" << YAML::Value << r->m_attachment0Name;
            if (!r->m_attachment1Name.empty()) out << YAML::Key << "Attachment1" << YAML::Value << r->m_attachment1Name;
            out << YAML::Key << "Color"     << YAML::Value << YAML::Flow << YAML::BeginSeq << r->Color.r << r->Color.g << r->Color.b << r->Color.a << YAML::EndSeq;
            out << YAML::Key << "LineWidth" << YAML::Value << r->LineWidth;
        }
        if (inst->IsA("BallSocket")) {
            BallSocket* bs = static_cast<BallSocket*>(inst);
            bs->refreshRefNames();
            out << YAML::Key << "Cube0" << YAML::Value << bs->m_cube0Name;
            out << YAML::Key << "Cube1" << YAML::Value << bs->m_cube1Name;
            if (!bs->m_attachment0Name.empty()) out << YAML::Key << "Attachment0" << YAML::Value << bs->m_attachment0Name;
            if (!bs->m_attachment1Name.empty()) out << YAML::Key << "Attachment1" << YAML::Value << bs->m_attachment1Name;
        }
        if (inst->IsA("NoCollision")) {
            NoCollision* nc = static_cast<NoCollision*>(inst);
            nc->refreshRefNames();
            out << YAML::Key << "Cube0" << YAML::Value << nc->m_cube0Name;
            out << YAML::Key << "Cube1" << YAML::Value << nc->m_cube1Name;
        }
        if (inst->IsA("Weld")) {
            Weld* w = static_cast<Weld*>(inst);
            w->refreshRefNames();
            out << YAML::Key << "Cube0" << YAML::Value << w->m_cube0Name;
            out << YAML::Key << "Cube1" << YAML::Value << w->m_cube1Name;
        }
        if (inst->IsA("Motor")) {
            Motor* m = static_cast<Motor*>(inst);
            m->refreshRefNames();
            out << YAML::Key << "Cube0" << YAML::Value << m->m_cube0Name;
            out << YAML::Key << "Cube1" << YAML::Value << m->m_cube1Name;
            if (!m->m_attachment0Name.empty()) out << YAML::Key << "Attachment0" << YAML::Value << m->m_attachment0Name;
            if (!m->m_attachment1Name.empty()) out << YAML::Key << "Attachment1" << YAML::Value << m->m_attachment1Name;
            out << YAML::Key << "Axis"  << YAML::Value
                << YAML::Flow << YAML::BeginSeq
                << m->Axis.x << m->Axis.y << m->Axis.z
                << YAML::EndSeq;
            out << YAML::Key << "DriveVelocity" << YAML::Value << m->DriveVelocity;
            out << YAML::Key << "MaxForce"      << YAML::Value << m->MaxForce;
        }

        if (inst->getClassName() == "Force") {
            PropertyRegistry::saveProperties(out, inst, "Force");
        }

        if (inst->IsA("ScreenGuiObject")) {
            PropertyRegistry::saveProperties(out, inst, inst->getClassName());
        }
        if (inst->IsA("Workspace")) {
            const Workspace* ws = static_cast<const Workspace*>(inst);
            out << YAML::Key << "Gravity" << YAML::Value
                << YAML::Flow << YAML::BeginSeq
                << ws->Gravity.x << ws->Gravity.y << ws->Gravity.z
                << YAML::EndSeq;
            out << YAML::Key << "Wind" << YAML::Value
                << YAML::Flow << YAML::BeginSeq
                << ws->Wind.x << ws->Wind.y << ws->Wind.z
                << YAML::EndSeq;
            out << YAML::Key << "PhysicsEnabled" << YAML::Value << ws->PhysicsEnabled;
        }
        if (inst->IsA("WorldGuiObject")) {
            PropertyRegistry::saveProperties(out, inst, inst->getClassName());
        }
        if (inst->getClassName() == "Canvas") {
            PropertyRegistry::saveProperties(out, inst, "Canvas");
        }
        if (inst->getClassName() == "Highlight") {
            PropertyRegistry::saveProperties(out, inst, "Highlight");
        }
        if (inst->getClassName() == "User") {
            const User* usr = static_cast<const User*>(inst);
            const char* controlModeStr = usr->controlMode == User::ControlMode::Free      ? "Free"
                                        : usr->controlMode == User::ControlMode::Program   ? "Program"
                                                                                            : "Character";
            out << YAML::Key << "ControlMode" << YAML::Value << controlModeStr;
            out << YAML::Key << "Speed"             << YAML::Value << usr->speed;
            out << YAML::Key << "RotationSpeed"     << YAML::Value << usr->rotationSpeed;
            out << YAML::Key << "MouseRotationSpeed" << YAML::Value << usr->mouseRotationSpeed;
            out << YAML::Key << "CameraDistance"    << YAML::Value << usr->cameraDistance;
            out << YAML::Key << "ZoomSpeed"         << YAML::Value << usr->zoomSpeed;
            out << YAML::Key << "MouseZoomSpeed"    << YAML::Value << usr->mouseZoomSpeed;
        }
        if (inst->getClassName() == "System") {
            const System* sys = static_cast<const System*>(inst);
            out << YAML::Key << "MaxClonesPerFrame"        << YAML::Value << sys->MaxClonesPerFrame;
            out << YAML::Key << "MaxRestartsPerFrame"      << YAML::Value << sys->MaxRestartsPerFrame;
            out << YAML::Key << "MaxTasksPerFrame"         << YAML::Value << sys->MaxTasksPerFrame;
            out << YAML::Key << "ScriptLoopTimeoutSeconds" << YAML::Value << sys->ScriptLoopTimeoutSeconds;
            PropertyRegistry::saveProperties(out, sys, "System");  // BaseResolution
        }
        if (inst->getClassName() == "Tool") {
            const Tool* tool = static_cast<const Tool*>(inst);
            static const char* handNames[] = { "Right", "Left", "Both" };
            out << YAML::Key << "Hand" << YAML::Value << handNames[static_cast<int>(tool->Hand)];
            out << YAML::Key << "Position" << YAML::Value
                << YAML::Flow << YAML::BeginSeq
                << tool->Position.x << tool->Position.y << tool->Position.z
                << YAML::EndSeq;
            out << YAML::Key << "Rotation" << YAML::Value
                << YAML::Flow << YAML::BeginSeq
                << tool->Rotation.x << tool->Rotation.y << tool->Rotation.z << tool->Rotation.w
                << YAML::EndSeq;
            if (!tool->m_handleName.empty())
                out << YAML::Key << "Handle" << YAML::Value << tool->m_handleName;
        }
        if (inst->getClassName() == "IntValue")     PropertyRegistry::saveProperties(out, inst, "IntValue");
        if (inst->getClassName() == "BoolValue")     PropertyRegistry::saveProperties(out, inst, "BoolValue");
        if (inst->getClassName() == "Vector3Value")  PropertyRegistry::saveProperties(out, inst, "Vector3Value");
        if (inst->getClassName() == "Color4Value")   PropertyRegistry::saveProperties(out, inst, "Color4Value");
        if (inst->getClassName() == "NumberValue") {
            NumberValue* nv = static_cast<NumberValue*>(inst);
            out << YAML::Key << "Value" << YAML::Value << nv->Value;
        }
        if (inst->getClassName() == "QuaternionValue") {
            QuaternionValue* qv = static_cast<QuaternionValue*>(inst);
            out << YAML::Key << "Value" << YAML::Value
                << YAML::Flow << YAML::BeginSeq
                << qv->Value.x << qv->Value.y << qv->Value.z << qv->Value.w
                << YAML::EndSeq;
        }
        if (inst->getClassName() == "CFrameValue") {
            CFrameValue* cv = static_cast<CFrameValue*>(inst);
            out << YAML::Key << "Value" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Position" << YAML::Value
                << YAML::Flow << YAML::BeginSeq
                << cv->Value.Position.x << cv->Value.Position.y << cv->Value.Position.z
                << YAML::EndSeq;
            out << YAML::Key << "Rotation" << YAML::Value
                << YAML::Flow << YAML::BeginSeq
                << cv->Value.Rotation.x << cv->Value.Rotation.y << cv->Value.Rotation.z << cv->Value.Rotation.w
                << YAML::EndSeq;
            out << YAML::EndMap;
        }
        if (inst->getClassName() == "ObjectValue") {
            ObjectValue* ov = static_cast<ObjectValue*>(inst);
            ov->refreshRefName();
            out << YAML::Key << "Value" << YAML::Value << ov->m_targetPathName;
        }

        out << YAML::EndMap;
    }

    // 子要素。Weatherの自動生成子（WeatherSkyAnchor等）も通常通り保存する。
    // ensureChildren() が既存の子を名前で採用（adopt）するため、再読込時に重複しない。
    if (!inst->children.empty()) {
        out << YAML::Key << "Children" << YAML::Value << YAML::BeginSeq;
        for (auto const& [name, child] : inst->children) {
            if (child && child->IsA("ChatService")) continue;
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

    // Root(System)自身はChildrenの一部として保存されない仮想的な親のため、
    // そのプロパティはここで別途保存する
    if (root->getClassName() == "System") {
        const System* sys = static_cast<const System*>(root);
        out << YAML::Key << "Properties" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "MaxClonesPerFrame"        << YAML::Value << sys->MaxClonesPerFrame;
        out << YAML::Key << "MaxRestartsPerFrame"      << YAML::Value << sys->MaxRestartsPerFrame;
        out << YAML::Key << "ScriptLoopTimeoutSeconds" << YAML::Value << sys->ScriptLoopTimeoutSeconds;
        PropertyRegistry::saveProperties(out, sys, "System");  // BaseResolution
        out << YAML::EndMap;
    }

    // Root は仮想的な親。その全ての子を Children として保存
    if (!root->children.empty()) {
        out << YAML::Key << "Children" << YAML::Value << YAML::BeginSeq;
        for (auto const& [name, child] : root->children) {
            if (child && child->IsA("ChatService")) continue;
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
