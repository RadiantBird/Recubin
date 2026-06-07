#pragma once
#include <include/Instances/Instance.hpp>
#include <Util/Color4.hpp>
#include <string>

class CharacterSetting : public Instance {
public:
    std::string facePath;
    Color4 headColor     = Color4(1.0f, 1.0f, 1.0f, 1.0f);
    Color4 torsoColor    = Color4::FromRGB(100, 12, 32);
    Color4 leftArmColor  = Color4(1.0f, 1.0f, 1.0f, 1.0f);
    Color4 rightArmColor = Color4(1.0f, 1.0f, 1.0f, 1.0f);
    Color4 leftLegColor  = Color4::FromRGB(0, 36, 81);
    Color4 rightLegColor = Color4::FromRGB(0, 36, 81);
    float jumpPower = 7.0f;
    float moveSpeed = 5.0f;

    CharacterSetting();
    virtual std::string getClassName() override { return "CharacterSetting"; }
    virtual bool IsA(std::string name) override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    virtual std::shared_ptr<Instance> clone() const override;
};
