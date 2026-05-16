#include <Instances/Texture.hpp>
#include <Core/Renderer.hpp>

static const char* faceNames[] = { "Front", "Back", "Top", "Bottom", "Right", "Left" };

Texture::Texture(unsigned int textureID, Face targetFace)
    : Instance("Texture_" + std::string(faceNames[(int)targetFace])), TextureID(textureID), face(targetFace) {}

Texture::~Texture() {}

std::string Texture::GetClassName() { return "Texture"; }

bool Texture::IsA(std::string className) {
    if (className == "Texture") return true;
    return Instance::IsA(className);
}

std::shared_ptr<Instance> Texture::clone() const {
    auto copy = std::make_shared<Texture>(this->TextureID, this->face);
    copy->Name          = this->Name;
    copy->texturePath   = this->texturePath;
    copy->Color         = this->Color;
    copy->StudsPerTileU = this->StudsPerTileU;
    copy->StudsPerTileV = this->StudsPerTileV;
    return copy;
}

void Texture::setFace(Face f) {
    face = f;
    Name = "Texture_" + std::string(faceNames[(int)f]);
}

void Texture::setProperty(const std::string& name, const YAML::Node& value) {
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
    } else if (name == "StudsPerTileU") {
        this->StudsPerTileU = value.as<float>();
    } else if (name == "StudsPerTileV") {
        this->StudsPerTileV = value.as<float>();
    } else {
        Instance::setProperty(name, value);
    }
}
