#include <include/Instances/PostEffect.hpp>
#include <include/Core/PropertyRegistry.hpp>

namespace {
// 以前のプロパティ表は Custom を 7 として書き出していた（PostEffectKind::Custom は 255）。
// 7 は他の種類に割り当てていないので、読み込み時は Custom として扱う。
constexpr int LEGACY_CUSTOM_TYPE = 7;
}

static const bool s_postEffectRegistered = []{
    using namespace PropertyRegistry;
    registerClass("PostEffect", {
        field<&PostEffect::Enabled>  ("Enabled"),
        enumProp<&PostEffect::Type>("Type", {{"None", 0}, {"CRT", 1}, {"Posterization", 2}, {"Pixelize", 3}, {"Saturation", 4}, {"VHS", 5}, {"ChromaticAberration", 6}, {"Custom", static_cast<int>(PostEffectKind::Custom)}}),
        field<&PostEffect::ZIndex>   ("ZIndex"),
        field<&PostEffect::Intensity>("Intensity"),
        field<&PostEffect::Param1>   ("Param1"),
        field<&PostEffect::Param2>   ("Param2"),
        field<&PostEffect::FragmentShaderFile>("FragmentShaderFile").omitEmpty().filePath("Fragment shader (*.frag;*.fs;*.glsl)", "*.frag;*.fs;*.glsl"),
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
    if (PropertyRegistry::loadProperty(this, "PostEffect", name, value)) {
        if (name == "Type" && static_cast<int>(Type) == LEGACY_CUSTOM_TYPE) Type = PostEffectKind::Custom;
        return;
    }
    Instance::setProperty(name, value);
}
