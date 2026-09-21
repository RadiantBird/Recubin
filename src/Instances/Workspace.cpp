#include <Instances/Workspace.hpp>
#include <Instances/BaseCube.hpp>
#include <Instances/SurfaceMark.hpp>
#include <Instances/LightSource.hpp>
#include <Instances/ParticleEmitter.hpp>
#include <Instances/ScreenGuiObject.hpp>
#include <Instances/WorldGuiObject.hpp>
#include <Instances/SurfaceGui.hpp>
#include <Instances/PostEffect.hpp>
#include <Instances/Highlight.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/Weather.hpp>
#include <Core/Terrain.hpp>
#include <Core/Physics.hpp>
#include <Core/PropertyRegistry.hpp>
#include <Util/Logger.hpp>
#include <algorithm>

static const bool s_workspaceRegistered = []{
    using namespace PropertyRegistry;
    registerClass("Workspace", {
        method_prop<&Workspace::getGravity, &Workspace::setGravity>("Gravity"),
        field<&Workspace::Wind>("Wind"),
        method_prop<&Workspace::getPhysicsEnabled, &Workspace::setPhysicsEnabled>("PhysicsEnabled"),
    });
    return true;
}();

void Workspace::registerScript(const std::shared_ptr<Instance>& s) {
    scripts.push_back(s);
}

void Workspace::unregisterScript(const std::shared_ptr<Instance>& s) {
    scripts.erase(std::remove(scripts.begin(), scripts.end(), s), scripts.end());
}

void Workspace::registerCube(const std::shared_ptr<Instance>& c) {
    if (std::find(pendingInstances.begin(), pendingInstances.end(), c) == pendingInstances.end()) {
        pendingInstances.push_back(c);
    }
}

void Workspace::unregisterCube(const Instance* c) {
    pendingInstances.erase(
        std::remove_if(pendingInstances.begin(), pendingInstances.end(),
            [c](const std::shared_ptr<Instance>& value) {
                return !value || value.get() == c;
            }),
        pendingInstances.end());
}

void Workspace::registerConstraint(const std::shared_ptr<Instance>& c) {
    if (std::find(pendingConstraints.begin(), pendingConstraints.end(), c) == pendingConstraints.end()) {
        pendingConstraints.push_back(c);
    }
}

void Workspace::unregisterConstraint(const Instance* c) {
    pendingConstraints.erase(
        std::remove_if(pendingConstraints.begin(), pendingConstraints.end(),
            [c](const std::shared_ptr<Instance>& value) {
                return !value || value.get() == c;
            }),
        pendingConstraints.end());
}

Workspace::Workspace() : Instance("Workspace") {}

void Workspace::registerRenderSubtree(Instance* root) {
    if (!root || root == this) return;

    m_renderInstances.push_back(root);
    if (root->IsA("BaseCube")) m_renderBaseCubes.push_back(static_cast<BaseCube*>(root));
    if (root->IsA("SurfaceMark")) m_renderSurfaceMarks.push_back(static_cast<SurfaceMark*>(root));
    if (root->IsA("LightSource")) m_renderLights.push_back(static_cast<LightSource*>(root));
    if (root->IsA("ParticleEmitter")) m_renderParticleEmitters.push_back(static_cast<ParticleEmitter*>(root));
    if (root->IsA("ScreenGuiObject")) m_renderScreenGuiObjects.push_back(static_cast<ScreenGuiObject*>(root));
    if (root->IsA("WorldGuiObject")) m_renderWorldGuiObjects.push_back(static_cast<WorldGuiObject*>(root));
    if (root->IsA("SurfaceGui")) m_renderSurfaceGuis.push_back(static_cast<SurfaceGui*>(root));
    if (root->IsA("PostEffect")) m_renderPostEffects.push_back(static_cast<PostEffect*>(root));
    if (root->IsA("Highlight")) m_renderHighlights.push_back(static_cast<Highlight*>(root));
    if (root->IsA("Lighting")) m_renderLightings.push_back(static_cast<Lighting*>(root));
    if (root->IsA("Weather")) m_renderWeathers.push_back(static_cast<Weather*>(root));
    if (root->IsA("Terrain")) m_renderTerrains.push_back(static_cast<Terrain*>(root));

    for (const auto& [name, child] : root->getChildren()) {
        (void)name;
        registerRenderSubtree(child.get());
    }
}

void Workspace::unregisterRenderSubtree(Instance* root) {
    if (!root || root == this) return;

    auto remove = [root](auto& values) {
        values.erase(std::remove(values.begin(), values.end(), root), values.end());
    };
    remove(m_renderInstances);
    if (root->IsA("BaseCube")) remove(m_renderBaseCubes);
    if (root->IsA("SurfaceMark")) remove(m_renderSurfaceMarks);
    if (root->IsA("LightSource")) remove(m_renderLights);
    if (root->IsA("ParticleEmitter")) remove(m_renderParticleEmitters);
    if (root->IsA("ScreenGuiObject")) remove(m_renderScreenGuiObjects);
    if (root->IsA("WorldGuiObject")) remove(m_renderWorldGuiObjects);
    if (root->IsA("SurfaceGui")) remove(m_renderSurfaceGuis);
    if (root->IsA("PostEffect")) remove(m_renderPostEffects);
    if (root->IsA("Highlight")) remove(m_renderHighlights);
    if (root->IsA("Lighting")) remove(m_renderLightings);
    if (root->IsA("Weather")) remove(m_renderWeathers);
    if (root->IsA("Terrain")) remove(m_renderTerrains);

    for (const auto& [name, child] : root->getChildren()) {
        (void)name;
        unregisterRenderSubtree(child.get());
    }
}

void Workspace::setGravity(const Vector3& value) {
    Gravity = value;
    if (physicsEngine) physicsEngine->setGravity(value);
}

Workspace::~Workspace() {
    m_renderInstances.clear();
    m_renderBaseCubes.clear();
    m_renderSurfaceMarks.clear();
    m_renderLights.clear();
    m_renderParticleEmitters.clear();
    m_renderScreenGuiObjects.clear();
    m_renderWorldGuiObjects.clear();
    m_renderSurfaceGuis.clear();
    m_renderPostEffects.clear();
    m_renderHighlights.clear();
    m_renderLightings.clear();
    m_renderWeathers.clear();
    m_renderTerrains.clear();
    for (auto& [name, child] : children) {
        child->Parent = {};
        child->onAncestorChanged();
    }
    this->children.clear();
}

std::shared_ptr<Instance> Workspace::clone() const {
    auto copy = std::make_shared<Workspace>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "Workspace");
    for (const auto& [name, child] : children) {
        if (child) copy->addChild(child->clone());
    }
    return copy;
}

std::string Workspace::getClassName() {
    return "Workspace";
}

bool Workspace::IsA(std::string className) {
    if (className == "Workspace") {
        return true;
    }
    return Instance::IsA(className);
}

void Workspace::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "Workspace", name, value)) return;
    Instance::setProperty(name, value);
}

void Workspace::initPhysics() {
    if (m_ownedPhysics) return; // 既に初期化済み
    m_ownedPhysics = std::make_unique<Physics>();
    m_ownedPhysics->init();
    physicsEngine = m_ownedPhysics.get();
    if (!m_ownedPhysics->isAvailable())
        RCBN_ERROR("Workspace physics backend is unavailable");
}
