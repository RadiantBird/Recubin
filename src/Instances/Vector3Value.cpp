#include <include/Instances/Vector3Value.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <include/Core/LuauEngine.hpp>

static const bool s_vector3ValueRegistered = []{
    using namespace PropertyRegistry;
    registerClass("Vector3Value", "ValueBase", {
        custom("Value", PropType::Vec3,
            [](Instance* o) -> PropValue { return static_cast<Vector3Value*>(o)->Value; },
            [](Instance* o, const PropValue& v) {
                auto* self = static_cast<Vector3Value*>(o);
                self->Value = std::get<Vector3>(v);
                if (self->Changed) self->Changed->fire([v](lua_State* L) {
                    Vector3* p = (Vector3*)lua_newuserdata(L, sizeof(Vector3));
                    *p = std::get<Vector3>(v);
                    luaL_getmetatable(L, LuauEngine::RCBN_VEC3_METATABLE);
                    lua_setmetatable(L, -2);
                    return 1;
                });
            }),
    });
    return true;
}();

Vector3Value::Vector3Value() : Named<Vector3Value, ValueBase>("Vector3Value") {}

bool Vector3Value::IsA(std::string className) {
    if (className == "Vector3Value") return true;
    return ValueBase::IsA(className);
}

void Vector3Value::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "Vector3Value", name, value)) return;
    ValueBase::setProperty(name, value);
}

std::shared_ptr<Instance> Vector3Value::clone() const {
    auto copy = std::make_shared<Vector3Value>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "Vector3Value");
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
