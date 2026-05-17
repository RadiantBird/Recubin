#include <Instances/ScreenGuiObject.hpp>

ScreenGuiObject::ScreenGuiObject(std::string className) : Instance(className) {}

std::string ScreenGuiObject::GetClassName() { return "ScreenGuiObject"; }

bool ScreenGuiObject::IsA(std::string name) {
    if (name == "ScreenGuiObject") return true;
    return Instance::IsA(name);
}

void ScreenGuiObject::setProperty(const std::string& name, const YAML::Node& val) {
    if (name == "Active") {
        Active = val.as<bool>();
    } else if (name == "Position") {
        Position = { val[0].as<float>(), val[1].as<float>() };
    } else if (name == "Size") {
        Size = { val[0].as<float>(), val[1].as<float>() };
    } else if (name == "Norm") {
        std::string s = val.as<std::string>();
        NormType = (s == "Scale") ? Norm::Scale : Norm::Pixel;
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
