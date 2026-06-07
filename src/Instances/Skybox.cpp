#include <include/Instances/Skybox.hpp>
#include <include/Instances/Decal.hpp>
#include <include/Core/Renderer.hpp>

Skybox::Skybox() : Cube(Vector3(0,0,0), Vector3(5000.0f, 5000.0f, 5000.0f), 0) {
    Name = "Skybox";
    Anchored = true;
    CanCollide = false;
    CastShadow = false;
    Unlit = true;
    Color = Color4(1, 1, 1, 1);


}

std::string Skybox::getClassName() {
    return "Skybox";
}

bool Skybox::IsA(std::string className) {
    if (className == "Skybox") return true;
    return Cube::IsA(className);
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
    copy->Name = this->Name;
    copy->Anchored = this->Anchored;
    copy->CanCollide = this->CanCollide;
    copy->CastShadow = this->CastShadow;
    copy->Unlit = this->Unlit;
    copy->cframe = this->cframe;
    copy->Color = this->Color;
    for (int i = 0; i < 6; i++) {
        copy->skyboxPaths[i] = this->skyboxPaths[i];
    }
    for (auto const& [name, child] : children) {
        copy->addChild(child->clone());
    }
    return copy;
}

void Skybox::setSkyboxPath(int faceIndex, const std::string& path) {
    if (faceIndex < 0 || faceIndex >= 6) return;
    skyboxPaths[faceIndex] = path;

    // Mapping from UI face index (0-5) to Decal's Face enum
    // UI: 0: Right(+X), 1: Left(-X), 2: Top(+Y), 3: Bottom(-Y), 4: Front(+Z), 5: Back(-Z)
    // Cube faces: 0: Front(-Z), 1: Back(+Z), 2: Top(+Y), 3: Bottom(-Y), 4: Right(+X), 5: Left(-X)
    Face mapping[6] = { Face::Right, Face::Left, Face::Top, Face::Bottom, Face::Back, Face::Front };

    // Find the decal for this face and update its texture
    Face targetFace = mapping[faceIndex];
    for (auto const& [name, child] : children) {
        if (child->IsA("Decal")) {
            Decal* decal = static_cast<Decal*>(child.get());
            if (decal->face == targetFace) {
                decal->texturePath = path;
                if (!path.empty() && Renderer::instance) {
                    decal->TextureID = Renderer::instance->loadTexture(path.c_str());
                } else {
                    decal->TextureID = 0;
                }
                break;
            }
        }
    }
}
