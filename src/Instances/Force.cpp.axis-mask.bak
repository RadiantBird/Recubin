#include <include/Instances/Force.hpp>
#include <include/Core/PropertyRegistry.hpp>

// スキーマ駆動（エディター/YAML/clone/Luauを一元化。プロパティ追加はここに1行足すだけ）
static const bool s_forceRegistered = []{
    using namespace PropertyRegistry;
    registerClass("Force", {
        field<&Force::Enabled>("Enabled"),
        field<&Force::Torque>("Torque"),
        field<&Force::MaintainVelocity>("MaintainVelocity"),
        field<&Force::Value>("Value"),
    });
    return true;
}();

std::shared_ptr<Instance> Force::clone() const {
    auto c = std::make_shared<Force>();
    c->Name = Name;
    PropertyRegistry::cloneFields(this, c.get(), "Force");
    for (auto const& [n, ch] : children) c->addChild(ch->clone());
    return c;
}

void Force::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "Force", name, value)) return;
    Instance::setProperty(name, value);
}
