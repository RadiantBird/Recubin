#include <Instances/Tool.hpp>
#include <Core/User.hpp>

Tool::Tool(std::string name) : Instance(name) {
    Activated = std::make_shared<RCBNScriptSignal>();
}