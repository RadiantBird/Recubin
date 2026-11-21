#include <include/Instances/ValueBase.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_valueBaseRegistered = []{
    using namespace PropertyRegistry;
    registerClass("ValueBase", {
        sig<&ValueBase::Changed>("Changed"),
    });
    return true;
}();

ValueBase::ValueBase(std::string className) : Instance(className), Changed(std::make_shared<RCBNScriptSignal>()) {}

bool ValueBase::IsA(std::string className) {
    if (className == "ValueBase") return true;
    return Instance::IsA(className);
}

void ValueBase::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "ValueBase", name, value)) return;
    Instance::setProperty(name, value);
}
