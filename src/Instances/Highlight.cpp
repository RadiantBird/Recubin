#include <Instances/Highlight.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_highlightRegistered = []{
    using namespace PropertyRegistry;
    registerClass("Highlight", "Instance", {
        field<&Highlight::FillColor>("FillColor"),
        field<&Highlight::OutlineColor>("OutlineColor"),
        field<&Highlight::OutlineThickness>("OutlineThickness", 0.0f, 10.0f, 0.1f).clampLua(),
        field<&Highlight::Enabled>("Enabled"),
    });
    return true;
}();

Highlight::Highlight() : Named<Highlight, Instance>("Highlight") {}

bool Highlight::IsA(std::string name) {
    if (name == "Highlight") return true;
    return Instance::IsA(name);
}

void Highlight::setProperty(const std::string& name, const YAML::Node& val) {
    if (PropertyRegistry::loadProperty(this, "Highlight", name, val)) return;
    Instance::setProperty(name, val);
}

std::shared_ptr<Instance> Highlight::clone() const {
    auto copy = std::make_shared<Highlight>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "Highlight");
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
