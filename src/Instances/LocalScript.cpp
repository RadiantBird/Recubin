#include <Instances/LocalScript.hpp>
#include <Core/PropertyRegistry.hpp>

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
    PropertyRegistry::cloneFields(this, copy.get(), "LocalScript");
    // Path はclone時に再読込せず、元のソース／bytecode状態をそのまま復元する。
    copy->Source        = Source;
    copy->Path          = Path;
    copy->isPrecompiled = isPrecompiled;
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
