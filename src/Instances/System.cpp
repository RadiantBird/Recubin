#include <Instances/Instance.hpp>
#include <Instances/System.hpp>

System::System(std::string name) : Instance(name) {
    Heartbeat = std::make_shared<RCBNScriptSignal>();
}
std::string System::GetClassName() {
    return "System";
}
bool System::IsA(std::string className) {
    return className == "System" || Instance::IsA(className);
}