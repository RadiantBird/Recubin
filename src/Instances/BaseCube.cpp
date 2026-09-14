#include "include/Instances/BaseCube.hpp"
#include "include/Core/Physics.hpp"
#include "include/Core/SystemState.hpp"
#include "include/Util/Logger.hpp"
#include "include/Core/PropertyRegistry.hpp"
#include "include/Instances/Model.hpp"
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

// BaseCube の通常 property は schema を YAML / clone / editor / Luau で共有する。
static const bool s_baseCubeRegistered = []{
    using namespace PropertyRegistry;

    // Anchored: フィールド直読み + setAnchored() 経由の副作用書込（physicsEngine の actor 再生成）
    PropertyDesc anchored = custom("Anchored", PropType::Bool,
        [](Instance* o) { return PropValue(static_cast<BaseCube*>(o)->Anchored); },
        [](Instance* o, const PropValue& v) { static_cast<BaseCube*>(o)->setAnchored(std::get<bool>(v)); });
    anchored.group("Physics");

    // MassDensity: ドラッグ中はフィールド書込のみ、確定時に setMassDensity() で actor 再生成
    PropertyDesc massDensity = custom("MassDensity", PropType::Float,
        [](Instance* o) { return PropValue(static_cast<BaseCube*>(o)->MassDensity); },
        [](Instance* o, const PropValue& v) { static_cast<BaseCube*>(o)->setMassDensity(std::get<float>(v)); });
    massDensity.lo = 0.01f; massDensity.hi = 50.0f; massDensity.step = 0.01f;

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
    ccdMode.yamlEnumAsString = true;

    PropertyDesc locked = custom("Locked", PropType::Bool,
        [](Instance* o) { return PropValue(static_cast<BaseCube*>(o)->Locked);},
        [](Instance* o, const PropValue& v) { static_cast<BaseCube*>(o)->setLocked(std::get<bool>(v)); });
    locked.group("Editor");

    PropertyDesc lockFlags = custom("LockFlags", PropType::Int,
        [](Instance* object) {
            return PropValue(static_cast<int>(static_cast<BaseCube*>(object)->LockFlags));
        },
        [](Instance* object, const PropValue& value) {
            static_cast<BaseCube*>(object)->setLockFlags(
                static_cast<PhysicsLockFlags>(std::get<int>(value)));
        });
    lockFlags.noEditor().luaHidden();
    lockFlags.yamlReadWith([](Instance* object, const YAML::Node& value) {
        PhysicsLockFlags flags = PhysicsLockFlags::None;
        if (!value.IsSequence()) {
            RCBN_WARN("LockFlags must be a YAML sequence");
            return true;
        }
        for (const YAML::Node& item : value) {
            if (!item.IsScalar()) {
                RCBN_WARN("Ignoring non-scalar LockFlags value");
                continue;
            }
            const std::string flag = item.as<std::string>();
            if (flag == "LinearX") flags |= PhysicsLockFlags::LinearX;
            else if (flag == "LinearY") flags |= PhysicsLockFlags::LinearY;
            else if (flag == "LinearZ") flags |= PhysicsLockFlags::LinearZ;
            else if (flag == "AngularX") flags |= PhysicsLockFlags::AngularX;
            else if (flag == "AngularY") flags |= PhysicsLockFlags::AngularY;
            else if (flag == "AngularZ") flags |= PhysicsLockFlags::AngularZ;
            else RCBN_WARN("Ignoring unknown LockFlags value: " << flag);
        }
        static_cast<BaseCube*>(object)->setLockFlags(flags);
        return true;
    });
    lockFlags.yamlWriteWith([](YAML::Emitter& out, const Instance* object,
                               std::string_view yamlKey) {
        const auto* cube = static_cast<const BaseCube*>(object);
        out << YAML::Key << std::string(yamlKey) << YAML::Value
            << YAML::Flow << YAML::BeginSeq;
        for (const auto& [flag, name] : {
                 std::pair{PhysicsLockFlags::LinearX, "LinearX"},
                 std::pair{PhysicsLockFlags::LinearY, "LinearY"},
                 std::pair{PhysicsLockFlags::LinearZ, "LinearZ"},
                 std::pair{PhysicsLockFlags::AngularX, "AngularX"},
                 std::pair{PhysicsLockFlags::AngularY, "AngularY"},
                 std::pair{PhysicsLockFlags::AngularZ, "AngularZ"}}) {
            if (hasPhysicsLockFlag(cube->LockFlags, flag)) out << name;
        }
        out << YAML::EndSeq;
    });

    PropertyDesc shadowMode = custom("ShadowMode", PropType::Enum,
        [](Instance* o) { return PropValue(static_cast<int>(static_cast<BaseCube*>(o)->ShadowMode)); },
        [](Instance* o, const PropValue& v) {
            int value = std::get<int>(v);
            if (value < 0 || value > 2) value = 2;
            static_cast<BaseCube*>(o)->ShadowMode = static_cast<::ShadowMode>(value);
        });
    shadowMode.enumNames = { {"Always", 0}, {"Never", 1}, {"Normal", 2} };
    shadowMode.yamlEnumAsString = true;
    shadowMode.yamlReadWith([](Instance* object, const YAML::Node& value) {
        const std::string name = value.as<std::string>("");
        int mode = static_cast<int>(::ShadowMode::Normal);
        if (name == "Always") mode = static_cast<int>(::ShadowMode::Always);
        else if (name == "Never") mode = static_cast<int>(::ShadowMode::Never);
        else if (name != "Normal") {
            RCBN_WARN("Unknown ShadowMode value '" << name << "'; using Normal");
        }
        static_cast<BaseCube*>(object)->ShadowMode = static_cast<::ShadowMode>(mode);
        return true;
    });

    // MaterialType: プリセット選択で material 一式を上書きする
    PropertyDesc materialType = custom("MaterialType", PropType::Enum,
        [](Instance* o) { return PropValue(static_cast<int>(static_cast<BaseCube*>(o)->material.type)); },
        [](Instance* o, const PropValue& v) {
            static_cast<BaseCube*>(o)->setMaterial(Material::GetDefault(static_cast<MaterialType>(std::get<int>(v))));
        });
    materialType.enumNames = { {"Plastic", 0}, {"Wood", 1}, {"Metal", 2}, {"Stone", 3} };
    materialType.group("Material");

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
        return d;
    };

    registerClass("BaseCube", "Spatial", {
        field<&BaseCube::Color>("Color").group("Appearance"),
        field<&BaseCube::CastShadow>("CastShadow"),
        shadowMode,
        field<&BaseCube::Unlit>("Unlit"),
        field<&BaseCube::UseTriplanar>("UseTriplanar"),
        field<&BaseCube::TextureScale>("TextureScale", 0.01f, 100.0f, 0.01f),
        anchored,
        custom("CanCollide", PropType::Bool,
            [](Instance* o) { return PropValue(static_cast<BaseCube*>(o)->CanCollide); },
            [](Instance* o, const PropValue& v) { static_cast<BaseCube*>(o)->setCanCollide(std::get<bool>(v)); }),
        massDensity,
        ccdMode,
        materialType,
        frictionProp("StaticFriction", &Material::staticFriction),
        frictionProp("DynamicFriction", &Material::dynamicFriction),
        frictionProp("Restitution", &Material::restitution),
        locked,
        lockFlags,
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

bool BaseCube::shouldCastShadow(bool hasVisibleFallbackGeometry) const {
    if (!CastShadow || ShadowMode == ::ShadowMode::Never) return false;
    if (ShadowMode == ::ShadowMode::Always) return true;
    return Color.a > 0.001f || hasVisibleFallbackGeometry;
}

void BaseCube::onAncestorChanged() {
    std::uint32_t newCharacterCollisionGroup = 0;
    for (auto ancestor = Parent.lock(); ancestor; ancestor = ancestor->Parent.lock()) {
        if (!ancestor->IsA("Model")) continue;
        const auto* model = static_cast<const Model*>(ancestor.get());
        if (model->m_characterCollisionGroup == 0) continue;
        newCharacterCollisionGroup = model->m_characterCollisionGroup;
        break;
    }
    if (m_characterCollisionGroup != newCharacterCollisionGroup) {
        m_characterCollisionGroup = newCharacterCollisionGroup;
        if (m_physicsOwner) m_physicsOwner->refreshCollisionFilter(*this);
    }

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

void BaseCube::setSize(const Vector3& newSize) {
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

void BaseCube::setCFrame(const CFrame& value) {
    teleportTo(value.Position);
    setRotation(value.Rotation);
}

void BaseCube::setPosition(const Vector3& value) {
    teleportTo(value);
}

// localRot: 親 Spatial からの相対回転
void BaseCube::setRotation(const Quaternion& localRot) {
    if (!validQuaternion(localRot)) {
        RCBN_ERROR("Rejected invalid Rotation for " << Name);
        return;
    }
    const float dot = std::abs(
        getRotation().w * localRot.w + getRotation().x * localRot.x +
        getRotation().y * localRot.y + getRotation().z * localRot.z);
    if (dot >= 0.9999999f) return;
    commitCFrame(CFrame(getPosition(), localRot), SpatialUpdateOrigin::Physics);
    if (!lastWorkspace || !lastWorkspace->physicsEngine ||
        !lastWorkspace->physicsEngine->hasBody(*this)) return;
    Quaternion worldRot = getWorldCFrame().Rotation;
    if (SystemState::get().isPlaying) {
        // Position と rotation は actor へ別々に送らず、member world pose として
        // 同じ pending/synchronization 経路で処理する。
        CFrame target(getWorldCFrame().Position, worldRot);
        lastWorkspace->physicsEngine->moveWeldAssembly(
            std::static_pointer_cast<BaseCube>(shared_from_this()), target);
    } else {
        lastWorkspace->physicsEngine->setMemberWorldCFrame(
            *this, CFrame(getWorldCFrame().Position, worldRot));
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
    if (m_physicsOwner && m_physicsOwner->hasBody(*this)) {
        // The Box3D shape remains present for mass and inertia. Toggling
        // collision therefore only changes its collision/query filter and
        // must not recreate the body or invalidate constraints.
        m_physicsOwner->refreshCollisionFilter(*this);
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
    commitCFrame(CFrame(localPos, getRotation()), SpatialUpdateOrigin::Physics);
    if (lastWorkspace && lastWorkspace->physicsEngine &&
        lastWorkspace->physicsEngine->hasBody(*this)) {
        CFrame memberTarget = getWorldCFrame();
        memberTarget.Position = getWorldCFrame().Position;
        lastWorkspace->physicsEngine->setMemberWorldCFrame(*this, memberTarget);
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
    if (PropertyRegistry::loadProperty(this, "BaseCube", name, value)) return;
    Spatial::setProperty(name, value);
}

void BaseCube::cloneBaseCubeStateAndChildrenTo(
    const std::shared_ptr<BaseCube>& copy) const {
    if (!copy) return;
    copy->Name = Name;
    for (const auto& [name, child] : children) {
        (void)name;
        copy->addChild(child->clone());
    }
}
