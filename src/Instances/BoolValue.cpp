#include <include/Instances/BoolValue.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_boolValueRegistered = []{
    using namespace PropertyRegistry;
    registerClass("BoolValue", "ValueBase", {
        custom("Value", PropType::Bool,
            [](Instance* o) -> PropValue { return static_cast<BoolValue*>(o)->Value; },
            [](Instance* o, const PropValue& v) {
                auto* self = static_cast<BoolValue*>(o);
                self->Value = std::get<bool>(v);
                if (self->Changed) self->Changed->fire([v](lua_State* L) {
                    lua_pushboolean(L, std::get<bool>(v) ? 1 : 0);
                    return 1;
                });
            }),
    });
    return true;
}();

BoolValue::BoolValue() : Named<BoolValue, ValueBase>("BoolValue") {}

bool BoolValue::IsA(std::string className) {
    if (className == "BoolValue") return true;
    return ValueBase::IsA(className);
}

void BoolValue::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "BoolValue", name, value)) return;
    ValueBase::setProperty(name, value);
}

std::shared_ptr<Instance> BoolValue::clone() const {
    auto copy = std::make_shared<BoolValue>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "BoolValue");
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
