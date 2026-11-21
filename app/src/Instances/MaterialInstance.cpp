#include <include/Instances/MaterialInstance.hpp>
#include <include/Instances/BaseCube.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <include/Util/Logger.hpp>
#include <include/Instances/FileRef.hpp>
#include <algorithm>
#include <cmath>

namespace {

// 物理プロパティ共通の宣言。値が変わったときだけ書き込み、参照元の
// actorを再生成させる。非有限値・負値(allowZero=falseなら0も)は拒否してエラーログを残す。
PropertyDesc physicsProperty(std::string_view propName, float MaterialInstance::* field,
                             float lo, float hi, float step, bool allowZero = true) {
    PropertyDesc d = PropertyRegistry::custom(propName, PropType::Float,
        [field](Instance* o) {
            return PropValue(static_cast<MaterialInstance*>(o)->*field);
        },
        [field, propName, allowZero](Instance* o, const PropValue& v) {
            auto* material = static_cast<MaterialInstance*>(o);
            const float value = std::get<float>(v);
            if (!std::isfinite(value) || value < 0.0f || (!allowZero && value == 0.0f)) {
                RCBN_ERROR("Rejected invalid " << propName << " for Material " << material->Name);
                return;
            }
            if (material->*field == value) return;
            material->*field = value;
            material->notifyPhysicsChanged();
        });
    d.lo = lo; d.hi = hi; d.step = step;
    d.group("Physics");
    return d;
}

} // namespace

// プロパティ・スキーマ（単一の正）。Luau/YAML/clone/エディターを一括駆動。
static const bool s_materialRegistered = []{
    using namespace PropertyRegistry;

    PropertyDesc massDensity = physicsProperty("MassDensity", &MaterialInstance::MassDensity,
                                               0.01f, 50.0f, 0.01f, false);
    PropertyDesc staticFriction = physicsProperty("StaticFriction",
        &MaterialInstance::StaticFriction, 0.0f, 2.0f, 0.01f);
    // Box3Dは静摩擦を使わない。将来の実装に備えて保持し、YAMLのみ保存する。
    staticFriction.noEditor().luaHidden();

    registerClass("Material", {
        field<&MaterialInstance::Metallic>   ("Metallic",    0.0f, 1.0f, 0.01f).clampLua().group("PBR"),
        field<&MaterialInstance::Roughness>  ("Roughness",   0.0f, 1.0f, 0.01f).clampLua(),
        field<&MaterialInstance::Reflectance>("Reflectance", 0.0f, 1.0f, 0.01f).clampLua(),
        instanceRefField<&MaterialInstance::BaseColorMap>("BaseColorMap", "FileRef").group("Maps"),
        instanceRefField<&MaterialInstance::RoughnessMap>("RoughnessMap", "FileRef"),
        instanceRefField<&MaterialInstance::MetallicMap>("MetallicMap", "FileRef"),
        instanceRefField<&MaterialInstance::NormalMap>("NormalMap", "FileRef"),
        field<&MaterialInstance::TextureScale>("TextureScale", 0.1f, 256.0f, 0.1f).clampLua(),
        field<&MaterialInstance::NormalStrength>("NormalStrength", 0.0f, 4.0f, 0.05f).clampLua(),
        staticFriction,
        physicsProperty("DynamicFriction", &MaterialInstance::DynamicFriction, 0.0f, 2.0f, 0.01f),
        physicsProperty("Restitution", &MaterialInstance::Restitution, 0.0f, 2.0f, 0.01f),
        massDensity,
        field<&MaterialInstance::Conductive>("Conductive"),
    });
    return true;
}();

MaterialInstance::MaterialInstance() : Instance("Material") {}

MaterialInstance::~MaterialInstance() {
    // 参照元が描画ホットパスで使う生ポインタを、破棄前に無効化する。
    for (const auto& [raw, weak] : m_users) {
        (void)raw;
        if (auto cube = weak.lock()) cube->clearMaterialRaw(this);
    }
}

void MaterialInstance::onChildrenChanged() {
    Instance::onChildrenChanged();
    m_hasFaceVisuals = false;
    for (const auto& [name, child] : children) {
        (void)name;
        if (child && (child->IsA("Decal") || child->IsA("Texture"))) {
            m_hasFaceVisuals = true;
            break;
        }
    }
}

std::string MaterialInstance::getClassName() { return "Material"; }

bool MaterialInstance::IsA(std::string className) {
    if (className == "Material") return true;
    return Instance::IsA(className);
}

void MaterialInstance::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "Material", name, value)) return;
    Instance::setProperty(name, value);
}

std::shared_ptr<Instance> MaterialInstance::clone() const {
    auto copy = std::make_shared<MaterialInstance>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "Material");
    for (const auto& [name, child] : children) {
        (void)name;
        copy->addChild(child->clone());
    }
    return copy;
}

const std::string& MaterialInstance::mapReference(MapSlot slot) const {
    switch (slot) {
        case MapSlot::BaseColor: return BaseColorMap;
        case MapSlot::Roughness: return RoughnessMap;
        case MapSlot::Metallic:  return MetallicMap;
        case MapSlot::Normal:    return NormalMap;
    }
    return BaseColorMap;
}

bool MaterialInstance::hasMaps() const {
    return !BaseColorMap.empty() || !RoughnessMap.empty() ||
           !MetallicMap.empty() || !NormalMap.empty();
}

std::string MaterialInstance::resolveMapPath(MapSlot slot) {
    const std::string& reference = mapReference(slot);
    if (reference.empty()) return {};

    // Material自身の子孫として、無ければ最上位の祖先(System等)からのパスとして探す。
    Instance* found = getChildByPath(reference);
    if (!found) {
        Instance* top = this;
        for (auto parent = Parent.lock(); parent; parent = parent->Parent.lock()) top = parent.get();
        if (top != this) found = top->getChildByPath(reference);
    }
    if (!found || found->getClassName() != "FileRef") return {};
    return static_cast<PhysicalFileInstance*>(found)->Path;
}

Material MaterialInstance::applyPhysicsTo(const Material& base) const {
    Material result = base;
    result.staticFriction  = StaticFriction;
    result.dynamicFriction = DynamicFriction;
    result.restitution     = Restitution;
    return result;
}

void MaterialInstance::registerUser(const std::shared_ptr<BaseCube>& cube) {
    if (!cube) return;
    auto [it, inserted] = m_users.emplace(cube.get(), cube);
    // 破棄済みのCubeと同じアドレスに新しいCubeが来た場合は、古い項目を置き換える。
    if (!inserted && it->second.expired()) it->second = cube;
}

void MaterialInstance::unregisterUser(const BaseCube* cube) {
    m_users.erase(cube);
}

void MaterialInstance::forEachUser(const std::function<void(BaseCube&)>& fn) {
    // コールバック内でm_usersが変更されても安全なようにコピーを走査する。
    std::vector<std::weak_ptr<BaseCube>> snapshot;
    snapshot.reserve(m_users.size());
    for (const auto& [raw, weak] : m_users) {
        (void)raw;
        snapshot.push_back(weak);
    }
    for (const auto& weak : snapshot) {
        if (auto cube = weak.lock()) fn(*cube);
    }
}

void MaterialInstance::notifyPhysicsChanged() {
    forEachUser([](BaseCube& cube) { cube.refreshPhysicsFromMaterial(); });
}
