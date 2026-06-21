#include <include/Instances/PostEffect.hpp>

PostEffect::PostEffect() : Instance("PostEffect") {}

std::string PostEffect::getClassName() { return "PostEffect"; }

bool PostEffect::IsA(std::string className) {
    if (className == "PostEffect") return true;
    return Instance::IsA(className);
}

std::shared_ptr<Instance> PostEffect::clone() const {
    auto copy = std::make_shared<PostEffect>();
    copy->Enabled   = Enabled;
    copy->Type      = Type;
    copy->ZIndex    = ZIndex;
    copy->Intensity = Intensity;
    copy->Param1    = Param1;
    copy->Param2    = Param2;
    return copy;
}

void PostEffect::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Enabled") {
        Enabled = value.as<bool>();
    } else if (name == "Type") {
        Type = static_cast<PostEffectKind>(value.as<int>());
    } else if (name == "ZIndex") {
        ZIndex = value.as<int>();
    } else if (name == "Intensity") {
        Intensity = value.as<float>();
    } else if (name == "Param1") {
        Param1 = value.as<float>();
    } else if (name == "Param2") {
        Param2 = value.as<float>();
    } else {
        Instance::setProperty(name, value);
    }
}
