#include <Instances/WorldGuiObject.hpp>

WorldGuiObject::WorldGuiObject(std::string className) : Instance(className) {}

std::string WorldGuiObject::GetClassName() { return "WorldGuiObject"; }

bool WorldGuiObject::IsA(std::string name) {
    if (name == "WorldGuiObject") return true;
    return Instance::IsA(name);
}

void WorldGuiObject::setProperty(const std::string& name, const YAML::Node& val) {
    if (name == "Size") {
        Size = { val[0].as<float>(), val[1].as<float>() };
    } else if (name == "Norm") {
        std::string s = val.as<std::string>();
        NormType = (s == "Scale") ? Norm::Scale : Norm::Pixel;
    } else if (name == "Active") {
        Active = val.as<bool>();
    } else if (name == "Visible") {
        Visible = val.as<bool>();
    } else if (name == "BackgroundColor") {
        BackgroundColor = { val[0].as<float>(), val[1].as<float>(),
                            val[2].as<float>(), val[3].as<float>() };
    } else if (name == "Transparency") {
        setTransparency(val.as<float>());
    } else if (name == "ZIndex") {
        ZIndex = val.as<int>();
    } else {
        Instance::setProperty(name, val);
    }
}
