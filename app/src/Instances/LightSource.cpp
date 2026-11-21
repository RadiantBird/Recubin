#include <include/Instances/LightSource.hpp>
#include <include/Core/PropertyRegistry.hpp>

// 基底スキーマ（Point/Spot が継承）。clone 連鎖のためにも登録しておく。
static const bool s_lightSourceRegistered = []{
    using namespace PropertyRegistry;
    registerClass("LightSource", {
        field<&LightSource::lightColor>("Color"),
        field<&LightSource::brightness>("Brightness", 0.0f, 10.0f,  0.01f).clampLua(),
        field<&LightSource::range>     ("Range",      0.0f, 200.0f, 0.1f).clampLua(),
    });
    return true;
}();

LightSource::LightSource(std::string className) : Instance(className) {}

bool LightSource::IsA(std::string className) {
    if (className == "LightSource") return true;
    return Instance::IsA(className);
}

void LightSource::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "LightSource", name, value)) return;
    Instance::setProperty(name, value);
}
