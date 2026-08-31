#include <Instances/Instance.hpp>
#include <Instances/System.hpp>
#include <Instances/Workspace.hpp>
#include <Core/PropertyRegistry.hpp>
#include <algorithm>
#include <Util/UUID.hpp>

static const bool s_systemRegistered = []{
    using namespace PropertyRegistry;
    registerClass("System", {
        field<&System::BaseResolution>("BaseResolution", 1.f, 16384.f, 1.f).luaReadOnly(),
        field<&System::UseNetwork>("UseNetwork"),
        field<&System::ApplicationId>("ApplicationId").luaReadOnly().noEditor(),
        field<&System::EnableIOAPI>("EnableIOAPI").luaReadOnly(),
        field<&System::EnableIPCAPI>("EnableIPCAPI").luaReadOnly(),
        field<&System::EnableExternalFileAccess>("EnableExternalFileAccess").luaReadOnly(),
        enumProp<&System::DefaultCameraMode>("DefaultCameraMode", {
            {"Character", static_cast<int>(System::CameraMode::Character)},
            {"Free", static_cast<int>(System::CameraMode::Free)},
            {"Program", static_cast<int>(System::CameraMode::Program)},
        }, true).luaReadOnly().group("Runtime"),
    });
    return true;
}();

System::System(std::string name) : Instance(name) {
    ApplicationId = RecubinUUID::generate();
    Heartbeat = std::make_shared<RCBNScriptSignal>();
    NetworkRoleChanged = std::make_shared<RCBNScriptSignal>();
}

void System::registerScript(const std::shared_ptr<Instance>& s) {
    scripts.push_back(s);
}

void System::unregisterScript(const std::shared_ptr<Instance>& s) {
    scripts.erase(std::remove(scripts.begin(), scripts.end(), s), scripts.end());
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
    if (name == "ApplicationId") {
        if (value.IsScalar()) {
            const auto candidate = value.as<std::string>();
            if (RecubinUUID::isValid(candidate)) ApplicationId = candidate;
        }
        return;
    }
    if (PropertyRegistry::loadProperty(this, "System", name, value)) return;
    if (name == "MaxClonesPerFrame")        { MaxClonesPerFrame        = value.as<int>();   return; }
    if (name == "MaxRestartsPerFrame")      { MaxRestartsPerFrame      = value.as<int>();   return; }
    if (name == "MaxTasksPerFrame")         { MaxTasksPerFrame         = value.as<int>();   return; }
    if (name == "ScriptLoopTimeoutSeconds") { ScriptLoopTimeoutSeconds = value.as<float>(); return; }
    Instance::setProperty(name, value);
}
