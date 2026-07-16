#include <include/Instances/CFrameValue.hpp>
#include <include/Core/LuauEngine.hpp>

CFrameValue::CFrameValue() : Named<CFrameValue, ValueBase>("CFrameValue") {}

bool CFrameValue::IsA(std::string className) {
    if (className == "CFrameValue") return true;
    return ValueBase::IsA(className);
}

void CFrameValue::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Value") {
        // Position: [x,y,z], Rotation: [x,y,z,w]（Spatial.cpp と同じ書式）。欠けているキーは現状維持
        if (value["Position"] && value["Position"].IsSequence() && value["Position"].size() == 3) {
            const YAML::Node& pos = value["Position"];
            Value.Position = Vector3(pos[0].as<float>(), pos[1].as<float>(), pos[2].as<float>());
        }
        if (value["Rotation"] && value["Rotation"].IsSequence() && value["Rotation"].size() == 4) {
            const YAML::Node& rot = value["Rotation"];
            Value.Rotation = Quaternion(
                rot[3].as<float>(),  // w
                rot[0].as<float>(),  // x
                rot[1].as<float>(),  // y
                rot[2].as<float>()   // z
            );
        }
        if (Changed) Changed->fire([this](lua_State* L) {
            CFrame* p = (CFrame*)lua_newuserdata(L, sizeof(CFrame));
            *p = Value;
            luaL_getmetatable(L, LuauEngine::RCBN_CFRAME_METATABLE);
            lua_setmetatable(L, -2);
            return 1;
        });
    } else {
        ValueBase::setProperty(name, value);
    }
}

std::shared_ptr<Instance> CFrameValue::clone() const {
    auto copy = std::make_shared<CFrameValue>();
    copy->Name = Name;
    copy->Value = Value;
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
