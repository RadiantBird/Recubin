#pragma once
#include <include/Instances/Instance.hpp>

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
    Face face = Face::Front;

    Decal(unsigned int textureID = 0, Face targetFace = Face::Front);
    virtual ~Decal();

    virtual std::string GetClassName() override;
    virtual bool IsA(std::string className) override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    virtual std::shared_ptr<Instance> clone() const override;
};
