#include <include/Instances/CFrameValue.hpp>
#include <include/Core/LuauEngine.hpp>
#include <include/Core/PropertyRegistry.hpp>

namespace {

const bool s_cFrameValueRegistered = [] {
    PropertyRegistry::registerClass("CFrameValue", "ValueBase", {
        PropertyRegistry::custom("Value", PropType::CFrame,
            [](Instance* object) -> PropValue {
                return static_cast<CFrameValue*>(object)->Value;
            },
            [](Instance* object, const PropValue& value) {
                auto* self = static_cast<CFrameValue*>(object);
                self->Value = std::get<CFrame>(value);
                if (self->Changed) self->Changed->fire([self](lua_State* L) {
                    CFrame* p = (CFrame*)lua_newuserdata(L, sizeof(CFrame));
                    *p = self->Value;
                    luaL_getmetatable(L, LuauEngine::RCBN_CFRAME_METATABLE);
                    lua_setmetatable(L, -2);
                    return 1;
                });
            }),
    });
    return true;
}();

} // namespace

CFrameValue::CFrameValue() : Named<CFrameValue, ValueBase>("CFrameValue") {}

bool CFrameValue::IsA(std::string className) {
    if (className == "CFrameValue") return true;
    return ValueBase::IsA(className);
}

void CFrameValue::setProperty(const std::string& name, const YAML::Node& value) {
    if (!PropertyRegistry::loadProperty(this, ClassName, name, value)) {
        ValueBase::setProperty(name, value);
    }
}

std::shared_ptr<Instance> CFrameValue::clone() const {
    auto copy = std::make_shared<CFrameValue>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), ClassName);
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
