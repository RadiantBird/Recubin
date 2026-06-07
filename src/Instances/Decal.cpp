#include <include/Instances/Decal.hpp>
#include <include/Core/Renderer.hpp>

static const char* faceNames[] = { "Front", "Back", "Top", "Bottom", "Right", "Left" };

Decal::Decal(unsigned int textureID, Face targetFace) 
    : Instance("Decal_" + std::string(faceNames[(int)targetFace])), TextureID(textureID), face(targetFace) {}

Decal::~Decal() {}

std::string Decal::getClassName() {
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
    copy->Color       = this->Color;
    return copy;
}

void Decal::setFace(Face f) {
    face = f;
    Name = "Decal_" + std::string(faceNames[(int)f]);
}

void Decal::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Texture") {
        this->texturePath = value.as<std::string>();
        if (Renderer::instance) {
            this->TextureID = Renderer::instance->loadTexture(this->texturePath.c_str());
        }
    } else if (name == "Face") {
        setFace(static_cast<Face>(value.as<int>()));
    } else if (name == "Color") {
        Color4 c;
        c.r = value[0].as<float>();
        c.g = value[1].as<float>();
        c.b = value[2].as<float>();
        c.a = value[3].as<float>();
        this->Color = c;
    } else {
        Instance::setProperty(name, value);
    }
}
