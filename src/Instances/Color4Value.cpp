#include <include/Instances/Color4Value.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <include/Core/LuauEngine.hpp>

static const bool s_color4ValueRegistered = []{
    using namespace PropertyRegistry;
    registerClass("Color4Value", "ValueBase", {
        custom("Value", PropType::Color4,
            [](Instance* o) -> PropValue { return static_cast<Color4Value*>(o)->Value; },
            [](Instance* o, const PropValue& v) {
                auto* self = static_cast<Color4Value*>(o);
                self->Value = std::get<Color4>(v);
                if (self->Changed) self->Changed->fire([v](lua_State* L) {
                    Color4* p = (Color4*)lua_newuserdata(L, sizeof(Color4));
                    *p = std::get<Color4>(v);
                    luaL_getmetatable(L, LuauEngine::RCBN_COLOR4_METATABLE);
                    lua_setmetatable(L, -2);
                    return 1;
                });
            }),
    });
    return true;
}();

Color4Value::Color4Value() : Named<Color4Value, ValueBase>("Color4Value") {}

bool Color4Value::IsA(std::string className) {
    if (className == "Color4Value") return true;
    return ValueBase::IsA(className);
}

void Color4Value::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "Color4Value", name, value)) return;
    ValueBase::setProperty(name, value);
}

std::shared_ptr<Instance> Color4Value::clone() const {
    auto copy = std::make_shared<Color4Value>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "Color4Value");
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
