#include <Instances/Instance.hpp>
#include <Instances/System.hpp>
#include <Instances/Workspace.hpp>

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
