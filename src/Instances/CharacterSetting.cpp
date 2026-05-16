#include <Instances/CharacterSetting.hpp>
#include <yaml-cpp/yaml.h>

CharacterSetting::CharacterSetting() : Instance("CharacterSetting") {}

bool CharacterSetting::IsA(std::string name) {
    if (name == "CharacterSetting") return true;
    return Instance::IsA(name);
}

static Color4 parseColor4(const YAML::Node& node) {
    if (node.IsSequence() && node.size() == 4)
        return Color4(node[0].as<float>(), node[1].as<float>(),
                      node[2].as<float>(), node[3].as<float>());
    return Color4(1.0f, 1.0f, 1.0f, 1.0f);
}

void CharacterSetting::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "FacePath")      { facePath     = value.as<std::string>(); return; }
    if (name == "HeadColor")     { headColor     = parseColor4(value);     return; }
    if (name == "TorsoColor")    { torsoColor    = parseColor4(value);     return; }
    if (name == "LeftArmColor")  { leftArmColor  = parseColor4(value);     return; }
    if (name == "RightArmColor") { rightArmColor = parseColor4(value);     return; }
    if (name == "LeftLegColor")  { leftLegColor  = parseColor4(value);     return; }
    if (name == "RightLegColor") { rightLegColor = parseColor4(value);     return; }
    if (name == "JumpPower")     { jumpPower     = value.as<float>();       return; }
    if (name == "MoveSpeed")     { moveSpeed     = value.as<float>();       return; }
    Instance::setProperty(name, value);
}

std::shared_ptr<Instance> CharacterSetting::clone() const {
    auto copy = std::make_shared<CharacterSetting>();
    copy->facePath     = facePath;
    copy->headColor    = headColor;
    copy->torsoColor   = torsoColor;
    copy->leftArmColor = leftArmColor;
    copy->rightArmColor= rightArmColor;
    copy->leftLegColor = leftLegColor;
    copy->rightLegColor= rightLegColor;
    copy->jumpPower    = jumpPower;
    copy->moveSpeed    = moveSpeed;
    return copy;
}
