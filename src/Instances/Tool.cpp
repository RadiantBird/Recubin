#include <Instances/Tool.hpp>
#include <Core/User.hpp>

Tool::Tool(std::string name) : Instance(name) {
    Activated = std::make_shared<RCBNScriptSignal>();
}

void Tool::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Hand") {
        std::string s = value.as<std::string>();
        Hand = (s == "Left") ? ToolHand::Left : (s == "Both") ? ToolHand::Both : ToolHand::Right;
        return;
    }
    Instance::setProperty(name, value);
}