#include <Instances/SurfaceGui.hpp>

SurfaceGui::SurfaceGui() : WorldGuiObject("SurfaceGui") {}

std::string SurfaceGui::GetClassName() { return "SurfaceGui"; }

bool SurfaceGui::IsA(std::string name) {
    if (name == "SurfaceGui") return true;
    return WorldGuiObject::IsA(name);
}

static Face faceFromString(const std::string& s) {
    if (s == "Back")   return Face::Back;
    if (s == "Top")    return Face::Top;
    if (s == "Bottom") return Face::Bottom;
    if (s == "Right")  return Face::Right;
    if (s == "Left")   return Face::Left;
    return Face::Front;
}

void SurfaceGui::setProperty(const std::string& name, const YAML::Node& val) {
    if (name == "Face") {
        face = faceFromString(val.as<std::string>());
    } else {
        WorldGuiObject::setProperty(name, val);
    }
}

std::shared_ptr<Instance> SurfaceGui::clone() const {
    auto copy = std::make_shared<SurfaceGui>();
    copy->Name            = Name;
    copy->Size            = Size;
    copy->NormType        = NormType;
    copy->Active          = Active;
    copy->Visible         = Visible;
    copy->BackgroundColor = BackgroundColor;
    copy->ZIndex          = ZIndex;
    copy->face            = face;
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
