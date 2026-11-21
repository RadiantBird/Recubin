#include <Instances/SignalEvent.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_signalEventRegistered = []{
    using namespace PropertyRegistry;
    registerClass("SignalEvent", "Instance", {
        sig<&SignalEvent::Fired>("Fired"),
    });
    return true;
}();

SignalEvent::SignalEvent() : Named<SignalEvent, Instance>("SignalEvent"), Fired(std::make_shared<RCBNScriptSignal>()) {}

bool SignalEvent::IsA(std::string name) {
    if (name == "SignalEvent") return true;
    return Instance::IsA(name);
}

void SignalEvent::setProperty(const std::string& name, const YAML::Node& val) {
    if (PropertyRegistry::loadProperty(this, "SignalEvent", name, val)) return;
    Instance::setProperty(name, val);
}

std::shared_ptr<Instance> SignalEvent::clone() const {
    auto copy = std::make_shared<SignalEvent>();
    copy->Name = Name;
    // Firedは複製せず新規（Humanoid::Diedと同じ方針）
    PropertyRegistry::cloneFields(this, copy.get(), "SignalEvent");
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
