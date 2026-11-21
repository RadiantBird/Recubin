#include <Instances/ProximityPrompt.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_proximityPromptRegistered = []{
    using namespace PropertyRegistry;
    registerClass("ProximityPrompt", "BillboardGui", {
        field<&ProximityPrompt::KeyboardKeyCode>      ("KeyboardKeyCode"),
        field<&ProximityPrompt::HoldDuration>         ("HoldDuration",          0, 60, 0.1f),
        field<&ProximityPrompt::MaxActivationDistance>("MaxActivationDistance", 0, 1000, 0.5f),
        field<&ProximityPrompt::Enabled>              ("Enabled"),
        field<&ProximityPrompt::ActionText>           ("ActionText"),
        field<&ProximityPrompt::ObjectText>           ("ObjectText"),
        sig  <&ProximityPrompt::Triggered>            ("Triggered"),
    });
    return true;
}();

ProximityPrompt::ProximityPrompt() : Named<ProximityPrompt, BillboardGui>() {
    Name = "ProximityPrompt";
    Triggered = std::make_shared<RCBNScriptSignal>();
    BackgroundColor = { 0.08f, 0.08f, 0.08f, 0.85f };
}

bool ProximityPrompt::IsA(std::string name) {
    if (name == "ProximityPrompt") return true;
    return BillboardGui::IsA(name);
}

void ProximityPrompt::setProperty(const std::string& name, const YAML::Node& val) {
    if (PropertyRegistry::loadProperty(this, "ProximityPrompt", name, val)) return;
    BillboardGui::setProperty(name, val);
}

std::shared_ptr<Instance> ProximityPrompt::clone() const {
    auto copy = std::make_shared<ProximityPrompt>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "ProximityPrompt");  // BillboardGui→WorldGuiObject 分も集約
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
