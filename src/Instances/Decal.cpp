#include <include/Instances/Decal.hpp>
#include <include/Core/Renderer.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const char* faceNames[] = { "Front", "Back", "Top", "Bottom", "Right", "Left" };

static const bool s_decalRegistered = []{
    using namespace PropertyRegistry;
    PropertyDesc face = custom("Face", PropType::Enum,
        [](Instance* instance) {
            return PropValue(static_cast<int>(static_cast<Decal*>(instance)->face));
        },
        [](Instance* instance, const PropValue& value) {
            static_cast<Decal*>(instance)->setFace(static_cast<Face>(std::get<int>(value)));
        });
    face.enumNames = {{"Front", 0}, {"Back", 1}, {"Top", 2}, {"Bottom", 3},
                      {"Right", 4}, {"Left", 5}};
    face.noEditor();
    PropertyDesc mode = custom("Mode", PropType::Enum,
        [](Instance* instance) {
            return PropValue(static_cast<int>(static_cast<Decal*>(instance)->Mode));
        },
        [](Instance* instance, const PropValue& value) {
            static_cast<Decal*>(instance)->setMode(static_cast<DecalMode>(std::get<int>(value)));
        });
    mode.enumNames = {{"UV", 0}, {"Face", 1}};
    mode.noEditor();
    registerClass("Decal", "Instance", {
        custom("Texture", PropType::String,
            [](Instance* instance) {
                return PropValue(static_cast<Decal*>(instance)->texturePath);
            },
            [](Instance* instance, const PropValue& value) {
                static_cast<Decal*>(instance)->setTexturePath(std::get<std::string>(value));
            }).omitEmpty().filePath("Image (*.png;*.jpg;*.bmp;*.tga)", "*.png;*.jpg;*.bmp;*.tga"),
        face,
        field<&Decal::Color>("Color"),
        custom("UVCenter", PropType::Vec2,
            [](Instance* instance) {
                return PropValue(static_cast<Decal*>(instance)->UVCenter);
            },
            [](Instance* instance, const PropValue& value) {
                static_cast<Decal*>(instance)->setUVCenter(std::get<Vector2>(value));
            }).noEditor(),
        custom("UVRadius", PropType::Float,
            [](Instance* instance) {
                return PropValue(static_cast<Decal*>(instance)->UVRadius);
            },
            [](Instance* instance, const PropValue& value) {
                static_cast<Decal*>(instance)->setUVRadius(std::get<float>(value));
            }).noEditor(),
        mode,
    });
    return true;
}();

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
    copy->Name = this->Name;
    PropertyRegistry::cloneFields(this, copy.get(), "Decal");
    copy->TextureID = this->TextureID;
    return copy;
}

void Decal::setFace(Face f) {
    // 名前がまだ既定名(Decal_<Face>)のままの場合のみ追従させる。
    // ユーザーが改名済みなら、Face変更で勝手に上書きしない。
    std::string defaultName = "Decal_" + std::string(faceNames[(int)face]);
    if (Name == defaultName) {
        Name = "Decal_" + std::string(faceNames[(int)f]);
    }
    face = f;
}

void Decal::setMode(DecalMode mode) {
    Mode = mode;
}

void Decal::setUVCenter(const Vector2& center) {
    UVCenter = center;
}

void Decal::setUVRadius(float radius) {
    UVRadius = radius;
}

void Decal::setTexturePath(const std::string& path) {
    this->texturePath = path;
    if (Renderer::instance) {
        this->TextureID = Renderer::instance->loadTexture(this->texturePath.c_str());
    }
}

void Decal::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "Decal", name, value)) return;
    Instance::setProperty(name, value);
}
