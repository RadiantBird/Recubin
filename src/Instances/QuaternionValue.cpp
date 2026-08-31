#include <include/Instances/QuaternionValue.hpp>
#include <include/Core/LuauEngine.hpp>
#include <include/Core/PropertyRegistry.hpp>

namespace {

const bool s_quaternionValueRegistered = [] {
    PropertyRegistry::registerClass("QuaternionValue", "ValueBase", {
        PropertyRegistry::custom("Value", PropType::Quaternion,
            [](Instance* object) -> PropValue {
                return static_cast<QuaternionValue*>(object)->Value;
            },
            [](Instance* object, const PropValue& value) {
                auto* self = static_cast<QuaternionValue*>(object);
                self->Value = std::get<Quaternion>(value);
                if (self->Changed) self->Changed->fire([self](lua_State* L) {
                    Quaternion* p = (Quaternion*)lua_newuserdata(L, sizeof(Quaternion));
                    *p = self->Value;
                    luaL_getmetatable(L, LuauEngine::RCBN_QUATERNION_METATABLE);
                    lua_setmetatable(L, -2);
                    return 1;
                });
            }),
    });
    return true;
}();

} // namespace

QuaternionValue::QuaternionValue() : Named<QuaternionValue, ValueBase>("QuaternionValue") {}

bool QuaternionValue::IsA(std::string className) {
    if (className == "QuaternionValue") return true;
    return ValueBase::IsA(className);
}

void QuaternionValue::setProperty(const std::string& name, const YAML::Node& value) {
    if (!PropertyRegistry::loadProperty(this, ClassName, name, value)) {
        ValueBase::setProperty(name, value);
    }
}

std::shared_ptr<Instance> QuaternionValue::clone() const {
    auto copy = std::make_shared<QuaternionValue>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), ClassName);
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
