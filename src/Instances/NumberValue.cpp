#include <include/Instances/NumberValue.hpp>

NumberValue::NumberValue() : Named<NumberValue, ValueBase>("NumberValue") {}

bool NumberValue::IsA(std::string className) {
    if (className == "NumberValue") return true;
    return ValueBase::IsA(className);
}

void NumberValue::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Value") {
        Value = value.as<double>();
        if (Changed) Changed->fire([this](lua_State* L) {
            lua_pushnumber(L, static_cast<lua_Number>(Value));
            return 1;
        });
    } else {
        ValueBase::setProperty(name, value);
    }
}

std::shared_ptr<Instance> NumberValue::clone() const {
    auto copy = std::make_shared<NumberValue>();
    copy->Name = Name;
    copy->Value = Value;
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
