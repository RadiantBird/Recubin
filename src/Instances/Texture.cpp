#include <Instances/Texture.hpp>
#include <Core/Renderer.hpp>
#include <Core/PropertyRegistry.hpp>

static const char* faceNames[] = { "Front", "Back", "Top", "Bottom", "Right", "Left" };

static const bool s_textureRegistered = []{
    using namespace PropertyRegistry;
    registerClass("Texture", "Instance", {
        custom("Texture", PropType::String,
            [](Instance* instance) {
                return PropValue(static_cast<Texture*>(instance)->texturePath);
            },
            [](Instance* instance, const PropValue& value) {
                static_cast<Texture*>(instance)->setTexturePath(std::get<std::string>(value));
            }).omitEmpty().filePath("Image (*.png;*.jpg;*.bmp;*.tga)", "*.png;*.jpg;*.bmp;*.tga"),
        custom("Face", PropType::Int,
            [](Instance* instance) {
                return PropValue(static_cast<int>(static_cast<Texture*>(instance)->face));
            },
            [](Instance* instance, const PropValue& value) {
                static_cast<Texture*>(instance)->setFace(static_cast<Face>(std::get<int>(value)));
            }),
        field<&Texture::Color>("Color"),
        field<&Texture::StudsPerTileU>("StudsPerTileU"),
        field<&Texture::StudsPerTileV>("StudsPerTileV"),
    });
    return true;
}();

Texture::Texture(unsigned int textureID, Face targetFace)
    : Instance("Texture_" + std::string(faceNames[(int)targetFace])), TextureID(textureID), face(targetFace) {}

Texture::~Texture() {}

std::string Texture::getClassName() { return "Texture"; }

bool Texture::IsA(std::string className) {
    if (className == "Texture") return true;
    return Instance::IsA(className);
}

std::shared_ptr<Instance> Texture::clone() const {
    auto copy = std::make_shared<Texture>(this->TextureID, this->face);
    copy->Name = this->Name;
    PropertyRegistry::cloneFields(this, copy.get(), "Texture");
    copy->TextureID = this->TextureID;
    return copy;
}

void Texture::setFace(Face f) {
    // 名前がまだ既定名(Texture_<Face>)のままの場合のみ追従させる。
    // ユーザーが改名済みなら、Face変更で勝手に上書きしない。
    std::string defaultName = "Texture_" + std::string(faceNames[(int)face]);
    if (Name == defaultName) {
        Name = "Texture_" + std::string(faceNames[(int)f]);
    }
    face = f;
}

void Texture::setTexturePath(const std::string& path) {
    this->texturePath = path;
    if (Renderer::instance) {
        this->TextureID = Renderer::instance->loadTexture(this->texturePath.c_str());
    }
}

void Texture::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "Texture", name, value)) return;
    Instance::setProperty(name, value);
}
