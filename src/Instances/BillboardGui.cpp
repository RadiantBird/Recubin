#include <Instances/BillboardGui.hpp>

BillboardGui::BillboardGui() : WorldGuiObject("BillboardGui") {}

std::string BillboardGui::getClassName() { return "BillboardGui"; }

bool BillboardGui::IsA(std::string name) {
    if (name == "BillboardGui") return true;
    return WorldGuiObject::IsA(name);
}

void BillboardGui::setProperty(const std::string& name, const YAML::Node& val) {
    if (name == "Mode") {
        std::string s = val.as<std::string>();
        Mode = (s == "Focus") ? BillboardMode::Focus : BillboardMode::Parallel;
    } else {
        WorldGuiObject::setProperty(name, val);
    }
}

std::shared_ptr<Instance> BillboardGui::clone() const {
    auto copy = std::make_shared<BillboardGui>();
    copy->Name            = Name;
    copy->Size            = Size;
    copy->NormType        = NormType;
    copy->Active          = Active;
    copy->Visible         = Visible;
    copy->BackgroundColor = BackgroundColor;
    copy->ZIndex          = ZIndex;
    copy->Mode            = Mode;
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
