#include <include/Instances/NumberValue.hpp>
#include <include/Core/PropertyRegistry.hpp>

// Valueはdouble。PropValueはfloatまでなので、YAMLの読み書きとクローンはdoubleのまま扱い、
// get/setは(丸めを許容する)汎用経路用に置く。編集は専用の倍精度UIが担当する。
static const bool s_numberValueRegistered = [] {
    using namespace PropertyRegistry;
    PropertyDesc value = custom("Value", PropType::Float,
        [](Instance* object) -> PropValue {
            return static_cast<float>(static_cast<NumberValue*>(object)->Value);
        },
        [](Instance* object, const PropValue& v) {
            static_cast<NumberValue*>(object)->setValue(std::get<float>(v));
        });
    value.noEditor();
    value.yamlReadWith([](Instance* object, const YAML::Node& node) {
        static_cast<NumberValue*>(object)->setValue(node.as<double>());
        return true;
    });
    value.yamlWriteWith([](YAML::Emitter& out, const Instance* object, std::string_view yamlKey) {
        out << YAML::Key << std::string(yamlKey) << YAML::Value
            << static_cast<const NumberValue*>(object)->Value;
    });
    value.copyStateWith([](const Instance* source, Instance* destination) {
        static_cast<NumberValue*>(destination)->Value = static_cast<const NumberValue*>(source)->Value;
    });
    registerClass("NumberValue", "ValueBase", {value});
    return true;
}();

NumberValue::NumberValue() : Named<NumberValue, ValueBase>("NumberValue") {}

bool NumberValue::IsA(std::string className) {
    if (className == "NumberValue") return true;
    return ValueBase::IsA(className);
}

void NumberValue::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "NumberValue", name, value)) return;
    ValueBase::setProperty(name, value);
}

void NumberValue::setValue(double newValue) {
    Value = newValue;
    if (Changed) Changed->fire([this](lua_State* L) {
        lua_pushnumber(L, static_cast<lua_Number>(Value));
        return 1;
    });
}

std::shared_ptr<Instance> NumberValue::clone() const {
    auto copy = std::make_shared<NumberValue>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "NumberValue");
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
