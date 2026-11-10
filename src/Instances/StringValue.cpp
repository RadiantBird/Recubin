#include <include/Instances/StringValue.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_stringValueRegistered = [] {
    using namespace PropertyRegistry;
    registerClass("StringValue", "ValueBase", {
        custom("Value", PropType::String,
            [](Instance* object) -> PropValue {
                return static_cast<StringValue*>(object)->Value;
            },
            [](Instance* object, const PropValue& value) {
                auto* self = static_cast<StringValue*>(object);
                self->Value = std::get<std::string>(value);
                if (self->Changed) {
                    const std::string changedValue = self->Value;
                    self->Changed->fire([changedValue](lua_State* state) {
                        lua_pushlstring(state, changedValue.data(), changedValue.size());
                        return 1;
                    });
                }
            })
    });
    return true;
}();

StringValue::StringValue() : Named<StringValue, ValueBase>("StringValue") {}

bool StringValue::IsA(std::string className) {
    if (className == "StringValue") return true;
    return ValueBase::IsA(className);
}

void StringValue::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "StringValue", name, value)) return;
    ValueBase::setProperty(name, value);
}

std::shared_ptr<Instance> StringValue::clone() const {
    auto copy = std::make_shared<StringValue>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "StringValue");
    for (auto const& [name, child] : children)
        copy->addChild(child->clone());
    return copy;
}
