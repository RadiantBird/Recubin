#include <Instances/Instance.hpp>
#include <Instances/System.hpp>
#include <Instances/Workspace.hpp>
#include <Core/PropertyRegistry.hpp>

static const bool s_systemRegistered = []{
    using namespace PropertyRegistry;
    registerClass("System", {
        field<&System::BaseResolution>("BaseResolution", 1.f, 16384.f, 1.f).luaReadOnly(),
    });
    return true;
}();

System::System(std::string name) : Instance(name) {
    Heartbeat = std::make_shared<RCBNScriptSignal>();
}
std::string System::getClassName() {
    return "System";
}
bool System::IsA(std::string className) {
    return className == "System" || Instance::IsA(className);
}

void System::addChild(std::shared_ptr<Instance> child) {
    if (child && child->IsA("Workspace")) {
        std::string baseName = child->Name.empty() ? "Workspace" : child->Name;
        std::string uniqueName = baseName;
        int suffix = 1;
        while (children.count(uniqueName) > 0 && children[uniqueName] != child) {
            uniqueName = baseName + std::to_string(suffix++);
        }
        child->Name = uniqueName;
    }
    Instance::addChild(std::move(child));
}

void System::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "System", name, value)) return;
    if (name == "MaxClonesPerFrame")        { MaxClonesPerFrame        = value.as<int>();   return; }
    if (name == "MaxRestartsPerFrame")      { MaxRestartsPerFrame      = value.as<int>();   return; }
    if (name == "ScriptLoopTimeoutSeconds") { ScriptLoopTimeoutSeconds = value.as<float>(); return; }
    Instance::setProperty(name, value);
}
