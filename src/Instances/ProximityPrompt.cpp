#include <Instances/ProximityPrompt.hpp>

ProximityPrompt::ProximityPrompt() : BillboardGui() {
    Name = "ProximityPrompt";
    Triggered = std::make_shared<RCBNScriptSignal>();
    BackgroundColor = { 0.08f, 0.08f, 0.08f, 0.85f };
}

std::string ProximityPrompt::getClassName() { return "ProximityPrompt"; }

bool ProximityPrompt::IsA(std::string name) {
    if (name == "ProximityPrompt") return true;
    return BillboardGui::IsA(name);
}

void ProximityPrompt::setProperty(const std::string& name, const YAML::Node& val) {
    if (name == "KeyboardKeyCode") {
        KeyboardKeyCode = val.as<std::string>();
    } else if (name == "HoldDuration") {
        HoldDuration = val.as<float>();
    } else if (name == "MaxActivationDistance") {
        MaxActivationDistance = val.as<float>();
    } else if (name == "Enabled") {
        Enabled = val.as<bool>();
    } else if (name == "ActionText") {
        ActionText = val.as<std::string>();
    } else if (name == "ObjectText") {
        ObjectText = val.as<std::string>();
    } else {
        BillboardGui::setProperty(name, val);
    }
}

std::shared_ptr<Instance> ProximityPrompt::clone() const {
    auto copy = std::make_shared<ProximityPrompt>();
    copy->Name            = Name;
    copy->Size            = Size;
    copy->NormType        = NormType;
    copy->Active          = Active;
    copy->Visible         = Visible;
    copy->BackgroundColor = BackgroundColor;
    copy->ZIndex          = ZIndex;
    copy->Mode            = Mode;

    copy->KeyboardKeyCode       = KeyboardKeyCode;
    copy->HoldDuration          = HoldDuration;
    copy->MaxActivationDistance = MaxActivationDistance;
    copy->Enabled               = Enabled;
    copy->ActionText            = ActionText;
    copy->ObjectText            = ObjectText;

    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
