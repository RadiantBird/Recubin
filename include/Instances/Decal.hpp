#pragma once
#include <include/Instances/Instance.hpp>
#include <Util/Color4.hpp>
#include <string>

enum class Face {
    Front = 0,
    Back = 1,
    Top = 2,
    Bottom = 3,
    Right = 4,
    Left = 5
};

class Decal : public Instance {
public:
    unsigned int TextureID = 0;
    Face         face      = Face::Front;
    std::string  texturePath;
    Color4       Color;

    Decal(unsigned int textureID = 0, Face targetFace = Face::Front);
    virtual ~Decal();

    virtual std::string getClassName() override;
    virtual bool IsA(std::string className) override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    virtual std::shared_ptr<Instance> clone() const override;

    void setFace(Face f);
};
