#include <include/Instances/QuaternionValue.hpp>
#include <include/Core/LuauEngine.hpp>

QuaternionValue::QuaternionValue() : Named<QuaternionValue, ValueBase>("QuaternionValue") {}

bool QuaternionValue::IsA(std::string className) {
    if (className == "QuaternionValue") return true;
    return ValueBase::IsA(className);
}

void QuaternionValue::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Value") {
        // [x, y, z, w] 形式で保存された Quaternion を読み込む（Spatial.cpp の Rotation と同じ書式）
        if (value.IsSequence() && value.size() == 4) {
            Value = Quaternion(
                value[3].as<float>(),  // w
                value[0].as<float>(),  // x
                value[1].as<float>(),  // y
                value[2].as<float>()   // z
            );
            if (Changed) Changed->fire([this](lua_State* L) {
                Quaternion* p = (Quaternion*)lua_newuserdata(L, sizeof(Quaternion));
                *p = Value;
                luaL_getmetatable(L, LuauEngine::RCBN_QUATERNION_METATABLE);
                lua_setmetatable(L, -2);
                return 1;
            });
        }
    } else {
        ValueBase::setProperty(name, value);
    }
}

std::shared_ptr<Instance> QuaternionValue::clone() const {
    auto copy = std::make_shared<QuaternionValue>();
    copy->Name = Name;
    copy->Value = Value;
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
