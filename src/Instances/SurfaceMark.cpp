#include <include/Instances/SurfaceMark.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <include/Core/Renderer.hpp>
#include <include/Instances/BaseCube.hpp>

#include <cmath>
#include <algorithm>

namespace {
const bool s_surfaceMarkRegistered = [] {
    using namespace PropertyRegistry;
    registerClass("SurfaceMark", "Spatial", {
        field<&SurfaceMark::Color>("Color", 0.0f, 1.0f, 0.01f),
        enumProp<&SurfaceMark::FilterMode>("FilterMode", {{"Exclude", 0}, {"Include", 1}}, true),
        custom("TexturePath", PropType::String,
               [](Instance* object) {
                   return PropValue(static_cast<SurfaceMark*>(object)->texturePath);
               },
               [](Instance* object, const PropValue& value) {
                   static_cast<SurfaceMark*>(object)->setTexturePath(std::get<std::string>(value));
               }).yaml("Texture").omitEmpty().noEditor().luaReadOnly()
    });
    return true;
}();
}

SurfaceMark::SurfaceMark()
    : SurfaceMark(Vector3(0.0f, 0.0f, 0.0f), Vector3(4.0f, 4.0f, 4.0f)) {}

SurfaceMark::SurfaceMark(const Vector3& position, const Vector3& size)
    : Spatial(position, size, "SurfaceMark"), Color(1.0f, 1.0f, 1.0f, 1.0f) {}

std::string SurfaceMark::getClassName() {
    return "SurfaceMark";
}

bool SurfaceMark::IsA(std::string className) {
    if (className == "SurfaceMark") return true;
    return Spatial::IsA(className);
}

void SurfaceMark::setTexturePath(const std::string& path) {
    texturePath = path;
    TextureID = 0;
    if (Renderer::instance && !texturePath.empty())
        TextureID = Renderer::instance->loadTexture(texturePath.c_str());
}

void SurfaceMark::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "FilterInstances") {
        std::vector<std::string> paths;
        if (value && value.IsSequence()) for (const auto& item : value)
            if (item.IsScalar()) paths.push_back(item.as<std::string>());
        setFilterPaths(paths);
        return;
    }
    if (PropertyRegistry::loadProperty(this, "SurfaceMark", name, value)) return;
    Spatial::setProperty(name, value);
}

std::shared_ptr<Instance> SurfaceMark::clone() const {
    auto copy = std::make_shared<SurfaceMark>(Position, Size);
    copy->Name = Name;
    copy->cframe.Rotation = cframe.Rotation;
    PropertyRegistry::cloneFields(this, copy.get(), "SurfaceMark");
    copy->TextureID = TextureID;
    std::vector<std::shared_ptr<Instance>> refs;
    for (const auto& weak : m_filterInstances) refs.push_back(weak.lock());
    copy->setFilterState(refs, m_filterPaths);
    for (const auto& [name, child] : children) {
        if (child) copy->addChild(child->clone());
    }
    return copy;
}

void SurfaceMark::setFilterInstances(const std::vector<std::shared_ptr<Instance>>& instances) {
    m_filterInstances.clear();
    m_filterPaths.clear();
    for (const auto& instance : instances) {
        if (!instance) continue;
        bool duplicate = false;
        for (const auto& existing : m_filterInstances)
            if (auto locked = existing.lock(); locked && locked.get() == instance.get()) { duplicate = true; break; }
        if (!duplicate) m_filterInstances.emplace_back(instance);
    }
    refreshFilterPaths();
}

void SurfaceMark::setFilterPaths(const std::vector<std::string>& paths) {
    m_filterPaths.clear();
    for (const auto& path : paths) {
        if (path.empty() || std::find(m_filterPaths.begin(), m_filterPaths.end(), path) != m_filterPaths.end()) continue;
        m_filterPaths.push_back(path);
    }
    m_filterInstances.assign(m_filterPaths.size(), std::weak_ptr<Instance>{});
}

void SurfaceMark::setFilterState(const std::vector<std::shared_ptr<Instance>>& instances,
                                 const std::vector<std::string>& paths) {
    m_filterPaths.clear();
    for (const auto& path : paths)
        if (!path.empty() && std::find(m_filterPaths.begin(), m_filterPaths.end(), path) == m_filterPaths.end())
            m_filterPaths.push_back(path);
    m_filterInstances.assign(m_filterPaths.size(), std::weak_ptr<Instance>{});
    for (size_t i = 0; i < instances.size() && i < m_filterInstances.size(); ++i) {
        const auto& instance = instances[i];
        if (!instance) continue;
        bool duplicate = false;
        for (const auto& existing : m_filterInstances)
            if (auto locked = existing.lock(); locked && locked.get() == instance.get()) { duplicate = true; break; }
        if (!duplicate) m_filterInstances[i] = instance;
    }
    refreshFilterPaths();
}

void SurfaceMark::refreshFilterPaths() {
    for (size_t i = 0; i < m_filterInstances.size(); ++i)
        if (auto instance = m_filterInstances[i].lock()) {
            if (i >= m_filterPaths.size()) m_filterPaths.resize(i + 1);
            m_filterPaths[i] = instance->getWorkspaceRelativePath();
        }
}

void SurfaceMark::resolveFilterInstances(Instance* root) {
    if (!root) return;
    m_filterInstances.assign(m_filterPaths.size(), std::weak_ptr<Instance>{});
    for (size_t i = 0; i < m_filterPaths.size(); ++i) {
        const auto& path = m_filterPaths[i];
        Instance* base = findFirstAncestorWorkspace();
        if (!base) base = root;
        Instance* found = path == base->Name ? base : base->getChildByPath(path);
        if (!found && root != base) found = root->getChildByPath(path);
        if (found) {
            bool duplicate = false;
            for (const auto& existing : m_filterInstances) if (auto e = existing.lock(); e && e.get() == found) duplicate = true;
            if (!duplicate) m_filterInstances[i] = found->shared_from_this();
        } else {
            std::cerr << "[SurfaceMark] unresolved FilterInstances path: " << path << "\n";
        }
    }
}

bool SurfaceMark::allowsSurfaceTarget(const BaseCube& target) const {
    auto workspaceOf = [](const Instance* instance) -> const Instance* {
        for (const Instance* current = instance; current; ) {
            if (const_cast<Instance*>(current)->IsA("Workspace")) return current;
            auto parent = current->Parent.lock(); current = parent.get();
        }
        return nullptr;
    };
    if (workspaceOf(this) != workspaceOf(&target)) return false;
    if (m_filterPaths.empty() && m_filterInstances.empty()) return FilterMode == SurfaceMarkFilterMode::Exclude;
    bool matched = false;
    for (const auto& weak : m_filterInstances) if (auto filter = weak.lock()) {
        for (const Instance* current = &target; current; ) {
            if (current == filter.get()) { matched = true; break; }
            auto parent = current->Parent.lock(); current = parent.get();
        }
        if (matched) break;
    }
    return FilterMode == SurfaceMarkFilterMode::Include ? matched : !matched;
}

void SurfaceMark::remapClonedInstances(const CloneRemap& remap) {
    for (auto& weak : m_filterInstances) if (auto ref = weak.lock()) {
        auto it = remap.find(ref.get());
        if (it != remap.end()) weak = it->second;
    }
}

Vector3 SurfaceMark::getForward() const {
    return getWorldCFrame().lookVector();
}

bool SurfaceMark::intersectsSphere(const Vector3& sphereCenter, float sphereRadius) const {
    if (!std::isfinite(Size.x) || !std::isfinite(Size.y) || !std::isfinite(Size.z) ||
        Size.x <= 0.0f || Size.y <= 0.0f || Size.z <= 0.0f ||
        !std::isfinite(sphereRadius) || sphereRadius < 0.0f ||
        !std::isfinite(sphereCenter.x) || !std::isfinite(sphereCenter.y) ||
        !std::isfinite(sphereCenter.z)) return false;
    const CFrame world = getWorldCFrame();
    const Vector3 local = world.inverse().pointToWorld(sphereCenter);
    const Vector3 half(Size.x * 0.5f, Size.y * 0.5f, 0.0f);
    const float dx = std::max(0.0f, std::fabs(local.x) - half.x);
    const float dy = std::max(0.0f, std::fabs(local.y) - half.y);
    const float zDistance = local.z > 0.0f ? local.z : (local.z < -Size.z ? -Size.z - local.z : 0.0f);
    const float dz = std::max(0.0f, zDistance);
    return dx * dx + dy * dy + dz * dz <= sphereRadius * sphereRadius;
}
