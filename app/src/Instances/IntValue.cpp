#include <include/Instances/IntValue.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_intValueRegistered = []{
    using namespace PropertyRegistry;
    registerClass("IntValue", "ValueBase", {
        custom("Value", PropType::Int,
            [](Instance* o) -> PropValue { return static_cast<IntValue*>(o)->Value; },
            [](Instance* o, const PropValue& v) {
                auto* self = static_cast<IntValue*>(o);
                self->Value = std::get<int>(v);
                if (self->Changed) self->Changed->fire([v](lua_State* L) {
                    lua_pushinteger(L, static_cast<lua_Integer>(std::get<int>(v)));
                    return 1;
                });
            }),
    });
    return true;
}();

IntValue::IntValue() : Named<IntValue, ValueBase>("IntValue") {}

bool IntValue::IsA(std::string className) {
    if (className == "IntValue") return true;
    return ValueBase::IsA(className);
}

void IntValue::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "IntValue", name, value)) return;
    ValueBase::setProperty(name, value);
}

std::shared_ptr<Instance> IntValue::clone() const {
    auto copy = std::make_shared<IntValue>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "IntValue");
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
