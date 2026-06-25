#include <Instances/SurfaceGui.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <GL/glew.h>

static const bool s_surfaceGuiRegistered = []{
    using namespace PropertyRegistry;
    registerClass("SurfaceGui", "WorldGuiObject", {
        enumProp<&SurfaceGui::face>("Face",
            {{"Front",0},{"Back",1},{"Top",2},{"Bottom",3},{"Right",4},{"Left",5}},
            /*yamlAsString*/true),
    });
    return true;
}();

SurfaceGui::SurfaceGui() : Named<SurfaceGui, WorldGuiObject>("SurfaceGui") {}

SurfaceGui::~SurfaceGui() {
    if (m_fboID) glDeleteFramebuffers(1, &m_fboID);
    if (m_texID) glDeleteTextures(1, &m_texID);
}

bool SurfaceGui::IsA(std::string name) {
    if (name == "SurfaceGui") return true;
    return WorldGuiObject::IsA(name);
}

void SurfaceGui::setProperty(const std::string& name, const YAML::Node& val) {
    if (PropertyRegistry::loadProperty(this, "SurfaceGui", name, val)) return;
    WorldGuiObject::setProperty(name, val);
}

std::shared_ptr<Instance> SurfaceGui::clone() const {
    auto copy = std::make_shared<SurfaceGui>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "SurfaceGui");
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
