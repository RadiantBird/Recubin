#include "include/Instances/BaseCube.hpp"
#include "include/Core/Physics.hpp"
#include "include/Core/SystemState.hpp"
#include "include/Util/Logger.hpp"
#include "include/Core/PropertyRegistry.hpp"
#include <cmath>

namespace {
bool finiteVector3(const Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

bool finiteQuaternion(const Quaternion& value) {
    return std::isfinite(value.w) && std::isfinite(value.x) &&
           std::isfinite(value.y) && std::isfinite(value.z);
}

bool validQuaternion(const Quaternion& value) {
    if (!finiteQuaternion(value)) return false;
    const float lengthSquared = value.w * value.w + value.x * value.x +
        value.y * value.y + value.z * value.z;
    return std::isfinite(lengthSquared) && lengthSquared > 1.0e-12f;
}
}

// ─── エディターUI専用のプロパティスキーマ登録 ───
// BaseCube の YAML 保存(SceneLoader)と Luau ディスパッチ(LuauEngine_Dispatch)は
// それぞれ手書きの実装が既に存在するため、この登録は PropertiesPanel のインスペクタ
// 描画だけを駆動する。saveProperties("BaseCube") / applyToDispatch("BaseCube", ...) は
// どこからも呼ばないこと（呼ぶと手書き実装と二重化・衝突する）。誤って呼ばれても実害が
// 出ないよう、各プロパティは noYaml() で YAML 対象外にしてある。
static const bool s_baseCubeRegistered = []{
    using namespace PropertyRegistry;

    // Anchored: フィールド直読み + setAnchored() 経由の副作用書込（physicsEngine の actor 再生成）
    PropertyDesc anchored = custom("Anchored", PropType::Bool,
        [](Instance* o) { return PropValue(static_cast<BaseCube*>(o)->Anchored); },
        [](Instance* o, const PropValue& v) { static_cast<BaseCube*>(o)->setAnchored(std::get<bool>(v)); });
    anchored.group("Physics");
    anchored.noYaml();

    // MassDensity: ドラッグ中はフィールド書込のみ、確定時に setMassDensity() で actor 再生成
    PropertyDesc massDensity = custom("MassDensity", PropType::Float,
        [](Instance* o) { return PropValue(static_cast<BaseCube*>(o)->MassDensity); },
        [](Instance* o, const PropValue& v) { static_cast<BaseCube*>(o)->setMassDensity(std::get<float>(v)); });
    massDensity.lo = 0.01f; massDensity.hi = 50.0f; massDensity.step = 0.01f;
    massDensity.noYaml();

    PropertyDesc ccdMode = custom("CCDMode", PropType::Enum,
        [](Instance* o) {
            return PropValue(static_cast<int>(static_cast<BaseCube*>(o)->CollisionDetection));
        },
        [](Instance* o, const PropValue& v) {
            CCDMode mode = static_cast<CCDMode>(std::get<int>(v));
            if (mode != CCDMode::Bullet) mode = CCDMode::Default;
            static_cast<BaseCube*>(o)->setCCDMode(mode);
        });
    ccdMode.enumNames = { {"Default", 0}, {"Bullet", 1} };
    ccdMode.group("Physics");
    ccdMode.noYaml();

    PropertyDesc locked = custom("Locked", PropType::Bool,
        [](Instance* o) { return PropValue(static_cast<BaseCube*>(o)->Locked);},
        [](Instance* o, const PropValue& v) { static_cast<BaseCube*>(o)->setLocked(std::get<bool>(v)); });
    locked.group("Editor");
    locked.noYaml();

    // MaterialType: プリセット選択で material 一式を上書きする
    PropertyDesc materialType = custom("MaterialType", PropType::Enum,
        [](Instance* o) { return PropValue(static_cast<int>(static_cast<BaseCube*>(o)->material.type)); },
        [](Instance* o, const PropValue& v) {
            static_cast<BaseCube*>(o)->setMaterial(Material::GetDefault(static_cast<MaterialType>(std::get<int>(v))));
        });
    materialType.enumNames = { {"Plastic", 0}, {"Wood", 1}, {"Metal", 2}, {"Stone", 3} };
    materialType.group("Material");
    materialType.noYaml();

    // friction/restitution: ドラッグ中はフィールド書込のみ、確定時に setMaterial() で actor 再生成
    auto frictionProp = [](std::string_view propName, float Material::* field) {
        PropertyDesc d = custom(propName, PropType::Float,
            [field](Instance* o) { return PropValue(static_cast<BaseCube*>(o)->material.*field); },
            [field](Instance* o, const PropValue& v) {
                BaseCube* bc = static_cast<BaseCube*>(o);
                Material updated = bc->material;
                updated.*field = std::get<float>(v);
                bc->setMaterial(updated);
            });
        d.lo = 0.0f; d.hi = 2.0f; d.step = 0.01f;
        d.noYaml();
        return d;
    };

    registerClass("BaseCube", {
        field<&BaseCube::Color>("Color").group("Appearance").noYaml(),
        anchored,
        custom("CanCollide", PropType::Bool,
            [](Instance* o) { return PropValue(static_cast<BaseCube*>(o)->CanCollide); },
            [](Instance* o, const PropValue& v) { static_cast<BaseCube*>(o)->setCanCollide(std::get<bool>(v)); }).noYaml(),
        field<&BaseCube::CastShadow>("CastShadow").noYaml(),
        field<&BaseCube::Unlit>("Unlit").noYaml(),
        locked,
        massDensity,
        ccdMode,
        materialType,
        frictionProp("StaticFriction", &Material::staticFriction),
        frictionProp("DynamicFriction", &Material::dynamicFriction),
        frictionProp("Restitution", &Material::restitution),
    });
    return true;
}();

BaseCube::BaseCube(Vector3 Pos, Vector3 Sz)
    : Spatial(Pos, Sz, "BaseCube"), Color(1, 1, 1, 1) {
    Touched = std::make_shared<RCBNScriptSignal>();
}

bool BaseCube::IsA(std::string className) {
    if (className == "BaseCube") {
        return true;
    }
    return Spatial::IsA(className);
}

void BaseCube::onAncestorChanged() {
    // 1. 先祖を遡って Workspace を探す (O(h))
    Workspace* newWorkspace =
        static_cast<Workspace*>(findFirstAncestorWorkspace());

    // Folder/Model 間など、同じ Workspace 内の親変更で body を
    // 登録し直してはいけない。
    if (newWorkspace != lastWorkspace) {
        auto self = std::static_pointer_cast<BaseCube>(shared_from_this());
        Workspace* oldWorkspace = lastWorkspace;

        // 新 world に登録する前に旧 world を完全に離脱する。
        // Physics 停止中でも pending の strong reference は残さない。
        if (oldWorkspace) {
            oldWorkspace->unregisterCube(this);
            if (oldWorkspace->physicsEngine)
                oldWorkspace->physicsEngine->removeCube(self);
        }

        lastWorkspace = newWorkspace;
        if (newWorkspace) newWorkspace->registerCube(self);
    }

    // 2. 子階層への通知も継続（BaseCube の中に何か入っている場合のため）
    Instance::onAncestorChanged();
}

void BaseCube::setSize(Vector3 newSize) {
    if (!finiteVector3(newSize) || newSize.x <= 0.0f ||
        newSize.y <= 0.0f || newSize.z <= 0.0f) {
        RCBN_ERROR("Rejected invalid Size for " << Name);
        return;
    }
    if (Size == newSize) return;
    Size = newSize;
    if (lastWorkspace && lastWorkspace->physicsEngine) {
        auto self = std::static_pointer_cast<BaseCube>(shared_from_this());
        if (SystemState::get().isPlaying) {
            lastWorkspace->physicsEngine->enqueueResize(self);
        } else {
            lastWorkspace->physicsEngine->recreateActor(self);
        }
    }
}

// localRot: 親 Spatial からの相対回転
void BaseCube::setRotation(Quaternion localRot) {
    if (!validQuaternion(localRot)) {
        RCBN_ERROR("Rejected invalid Rotation for " << Name);
        return;
    }
    const float dot = std::abs(
        cframe.Rotation.w * localRot.w + cframe.Rotation.x * localRot.x +
        cframe.Rotation.y * localRot.y + cframe.Rotation.z * localRot.z);
    if (dot >= 0.9999999f) return;
    cframe.Rotation = localRot;
    if (!lastWorkspace || !lastWorkspace->physicsEngine ||
        !lastWorkspace->physicsEngine->hasBody(*this)) return;
    Quaternion worldRot = getWorldCFrame().Rotation;
    if (SystemState::get().isPlaying) {
        lastWorkspace->physicsEngine->enqueueSetRotation(std::static_pointer_cast<BaseCube>(shared_from_this()), worldRot);
    } else {
        CFrame bodyCFrame = lastWorkspace->physicsEngine->getBodyWorldCFrame(*this);
        bodyCFrame.Rotation = worldRot;
        lastWorkspace->physicsEngine->setBodyWorldCFrame(*this, bodyCFrame);
    }
}

void BaseCube::setAnchored(bool anchored) {
    if (Anchored == anchored) return;
    Anchored = anchored;
    if (lastWorkspace && lastWorkspace->physicsEngine) {
        lastWorkspace->physicsEngine->recreateActor(std::static_pointer_cast<BaseCube>(shared_from_this()));
    }
}

void BaseCube::setCanCollide(bool canCollide) {
    if (CanCollide == canCollide) return;
    CanCollide = canCollide;
    if (lastWorkspace && lastWorkspace->physicsEngine) {
        lastWorkspace->physicsEngine->recreateActor(std::static_pointer_cast<BaseCube>(shared_from_this()));
    }
}

void BaseCube::setLocked(bool locked) {
    if (Locked == locked) return;
    Locked = locked;
}

void BaseCube::setMaterial(const Material& m) {
    if (!std::isfinite(m.staticFriction) || !std::isfinite(m.dynamicFriction) ||
        !std::isfinite(m.restitution) || m.staticFriction < 0.0f ||
        m.dynamicFriction < 0.0f || m.restitution < 0.0f) {
        RCBN_ERROR("Rejected invalid Material for " << Name);
        return;
    }
    if (material.type == m.type && material.staticFriction == m.staticFriction &&
        material.dynamicFriction == m.dynamicFriction &&
        material.restitution == m.restitution)
        return;
    material = m;
    if (lastWorkspace && lastWorkspace->physicsEngine) {
        lastWorkspace->physicsEngine->recreateActor(std::static_pointer_cast<BaseCube>(shared_from_this()));
    }
}

void BaseCube::setMassDensity(float d) {
    if (!std::isfinite(d) || d <= 0.0f) {
        RCBN_ERROR("Rejected invalid MassDensity for " << Name);
        return;
    }
    if (MassDensity == d) return;
    MassDensity = d;
    if (lastWorkspace && lastWorkspace->physicsEngine) {
        lastWorkspace->physicsEngine->recreateActor(std::static_pointer_cast<BaseCube>(shared_from_this()));
    }
}

void BaseCube::setLockFlags(PhysicsLockFlags flags) {
    if (LockFlags == flags) return;
    LockFlags = flags;
    if (m_physicsOwner && m_physicsOwner->hasBody(*this))
        m_physicsOwner->applyLockFlags(*this);
}

void BaseCube::setCCDMode(CCDMode mode) {
    if (mode != CCDMode::Bullet) mode = CCDMode::Default;
    if (CollisionDetection == mode) return;
    CollisionDetection = mode;
    if (m_physicsOwner && m_physicsOwner->hasBody(*this)) {
        m_physicsOwner->recreateActor(std::static_pointer_cast<BaseCube>(shared_from_this()));
    }
}

void BaseCube::syncPhysics() {
    if (lastWorkspace && lastWorkspace->physicsEngine)
        lastWorkspace->physicsEngine->syncCube(*this);
}

// localPos: 親 Spatial からの相対座標
void BaseCube::teleportTo(Vector3 localPos) {
    cframe.Position = localPos;
    if (lastWorkspace && lastWorkspace->physicsEngine &&
        lastWorkspace->physicsEngine->hasBody(*this)) {
        CFrame bodyCFrame = lastWorkspace->physicsEngine->getBodyWorldCFrame(*this);
        bodyCFrame.Position = getWorldCFrame().Position;
        lastWorkspace->physicsEngine->setBodyWorldCFrame(*this, bodyCFrame);
    }
}

BaseCube::~BaseCube() {
    // RCBN_LOG("BaseCube Destructor: " << this->Name);
    // backend body の所有権と逆引き情報は、登録元の Physics が一元的に破棄する。
    if (m_physicsOwner) m_physicsOwner->onCubeDestroyed(*this);
}
unsigned int BaseCube::getDecalTexture(Face face, unsigned int fallback) const {
    for (auto const& [name, child] : children) {
        if (child->IsA("Decal")) {
            Decal* d = static_cast<Decal*>(child.get());
            if (d->face == face && d->TextureID != 0) {
                return d->TextureID;
            }
        }
    }
    return fallback;
}

void BaseCube::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Anchored") {
        setAnchored(value.as<bool>());
    } else if (name == "CanCollide") {
        setCanCollide(value.as<bool>());
    } else if (name == "Color") {
        Color4 color(0,0,0,0);
        color.r = value[0].as<float>();
        color.g = value[1].as<float>();
        color.b = value[2].as<float>();
        color.a = value[3].as<float>();
        this->Color = color;
    } else if (name == "CastShadow") {
        this->CastShadow = value.as<bool>();
    } else if (name == "Unlit") {
        this->Unlit = value.as<bool>();
    } else if (name == "UseTriplanar") {
        this->UseTriplanar = value.as<bool>();
    } else if (name == "TextureScale") {
        this->TextureScale = value.as<float>();
    } else if (name == "MaterialType") {
        material.type = static_cast<MaterialType>(value.as<int>());
    } else if (name == "StaticFriction") {
        material.staticFriction = value.as<float>();
    } else if (name == "DynamicFriction") {
        material.dynamicFriction = value.as<float>();
    } else if (name == "Restitution") {
        material.restitution = value.as<float>();
    } else if (name == "MassDensity") {
        setMassDensity(value.as<float>());
    } else if (name == "Locked") {
        setLocked(value.as<bool>());
    } else if (name == "CCDMode") {
        const std::string mode = value.as<std::string>();
        setCCDMode(mode == "Bullet" ? CCDMode::Bullet : CCDMode::Default);
    } else if (name == "LockFlags") {
        PhysicsLockFlags flags = PhysicsLockFlags::None;
        if (value.IsSequence()) {
            for (const YAML::Node& item : value) {
                const std::string flag = item.as<std::string>();
                if (flag == "LinearX") flags |= PhysicsLockFlags::LinearX;
                else if (flag == "LinearY") flags |= PhysicsLockFlags::LinearY;
                else if (flag == "LinearZ") flags |= PhysicsLockFlags::LinearZ;
                else if (flag == "AngularX") flags |= PhysicsLockFlags::AngularX;
                else if (flag == "AngularY") flags |= PhysicsLockFlags::AngularY;
                else if (flag == "AngularZ") flags |= PhysicsLockFlags::AngularZ;
                else RCBN_WARN("Ignoring unknown LockFlags value: " << flag);
            }
        }
        setLockFlags(flags);
    } else {
        Spatial::setProperty(name, value);
    }
}
