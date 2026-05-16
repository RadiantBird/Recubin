#pragma once
#include <include/Instances/Decal.hpp>
#include <Util/Color4.hpp>
#include <string>

class Texture : public Instance {
public:
    unsigned int TextureID     = 0;
    Face         face          = Face::Front;
    std::string  texturePath;
    Color4       Color;
    float        StudsPerTileU = 1.0f;
    float        StudsPerTileV = 1.0f;

    Texture(unsigned int textureID = 0, Face targetFace = Face::Front);
    virtual ~Texture();

    virtual std::string GetClassName() override;
    virtual bool IsA(std::string className) override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    virtual std::shared_ptr<Instance> clone() const override;

    void setFace(Face f);
};
