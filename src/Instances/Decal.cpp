#include <include/Instances/Decal.hpp>
#include <include/Core/Renderer.hpp>

static const char* faceNames[] = { "Front", "Back", "Top", "Bottom", "Right", "Left" };

Decal::Decal(unsigned int textureID, Face targetFace) 
    : Instance("Decal_" + std::string(faceNames[(int)targetFace])), TextureID(textureID), face(targetFace) {}

Decal::~Decal() {}

std::string Decal::GetClassName() {
    return "Decal";
}

bool Decal::IsA(std::string className) {
    if (className == "Decal") return true;
    return Instance::IsA(className);
}

std::shared_ptr<Instance> Decal::clone() const {
    auto copy = std::make_shared<Decal>(this->TextureID, this->face);
    copy->Name        = this->Name;
    copy->texturePath = this->texturePath;
    return copy;
}

void Decal::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Texture") {
        this->texturePath = value.as<std::string>();
        if (Renderer::instance) {
            this->TextureID = Renderer::instance->loadTexture(this->texturePath.c_str());
        }
    } else if (name == "Face") {
        this->face = static_cast<Face>(value.as<int>());
        this->Name = "Decal_" + std::string(faceNames[(int)this->face]);
    } else {
        Instance::setProperty(name, value);
    }
}
