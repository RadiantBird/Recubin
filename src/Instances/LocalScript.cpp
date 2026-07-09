#include <Instances/LocalScript.hpp>

LocalScript::LocalScript(string path) : Script(path) {
    Name = "LocalScript";
}

std::string LocalScript::getClassName() {
    return "LocalScript";
}

bool LocalScript::IsA(std::string className) {
    if (className == "LocalScript") {
        return true;
    }
    return Script::IsA(className);
}

std::shared_ptr<Instance> LocalScript::clone() const {
    auto copy = std::make_shared<LocalScript>();
    copy->Name          = Name;
    copy->Source        = Source;
    copy->Path          = Path;
    copy->Enabled       = Enabled;
    copy->isPrecompiled = isPrecompiled;
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
