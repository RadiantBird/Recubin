#include <include/Instances/Skybox.hpp>
#include <include/Instances/Decal.hpp>
#include <include/Core/Renderer.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_skyboxRegistered = [] {
    PropertyRegistry::registerClass("Skybox", "Cube", {});
    return true;
}();

Skybox::Skybox() : Named<Skybox, Cube>(Vector3(0,0,0), Vector3(5000.0f, 5000.0f, 5000.0f), TextureHandle{}) {
    Name = "Skybox";
    Anchored = true;
    CanCollide = false;
    CastShadow = false;
    Unlit = true;
    Color = Color4(1, 1, 1, 1);


}

void Skybox::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "SkyboxPaths" && value.IsSequence() && value.size() == 6) {
        for (int i = 0; i < 6; i++) {
            setSkyboxPath(i, value[i].as<std::string>());
        }
    } else {
        Cube::setProperty(name, value);
    }
}

std::shared_ptr<Instance> Skybox::clone() const {
    auto copy = std::make_shared<Skybox>();
    for (int i = 0; i < 6; i++) {
        copy->skyboxPaths[i] = this->skyboxPaths[i];
    }
    PropertyRegistry::cloneFields(this, copy.get(), "Skybox");
    cloneBaseCubeStateAndChildrenTo(copy);
    return copy;
}

void Skybox::setSkyboxPath(int faceIndex, const std::string& path) {
    if (faceIndex < 0 || faceIndex >= 6) return;
    skyboxPaths[faceIndex] = path;

    // Find the decal for this face and update its texture
    Face targetFace = IMAGE_FACES[faceIndex];
    for (auto const& [name, child] : children) {
        if (child->IsA("Decal")) {
            Decal* decal = static_cast<Decal*>(child.get());
            if (decal->face == targetFace) {
                decal->texturePath = path;
                if (!path.empty() && Renderer::instance) {
                    decal->TextureID = Renderer::instance->loadTexture(path.c_str());
                } else {
                    decal->TextureID = TextureHandle{};
                }
                break;
            }
        }
    }
}
