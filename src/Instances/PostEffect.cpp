#include <include/Instances/PostEffect.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_postEffectRegistered = []{
    using namespace PropertyRegistry;
    registerClass("PostEffect", {
        field<&PostEffect::Enabled>  ("Enabled"),
        field<&PostEffect::Type>     ("Type"),
        field<&PostEffect::ZIndex>   ("ZIndex"),
        field<&PostEffect::Intensity>("Intensity"),
        field<&PostEffect::Param1>   ("Param1"),
        field<&PostEffect::Param2>   ("Param2"),
    });
    return true;
}();

PostEffect::PostEffect() : Instance("PostEffect") {}

std::string PostEffect::getClassName() { return "PostEffect"; }

bool PostEffect::IsA(std::string className) {
    if (className == "PostEffect") return true;
    return Instance::IsA(className);
}

std::shared_ptr<Instance> PostEffect::clone() const {
    auto copy = std::make_shared<PostEffect>();
    PropertyRegistry::cloneFields(this, copy.get(), "PostEffect");
    return copy;
}

void PostEffect::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "PostEffect", name, value)) return;
    Instance::setProperty(name, value);
}
