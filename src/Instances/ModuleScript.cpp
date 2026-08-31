#include <Instances/ModuleScript.hpp>
#include <Core/PropertyRegistry.hpp>

ModuleScript::ModuleScript(string path) : Script(path) {
    Name = "ModuleScript";
}

std::string ModuleScript::getClassName() {
    return "ModuleScript";
}

bool ModuleScript::IsA(std::string className) {
    if (className == "ModuleScript") {
        return true;
    }
    return Script::IsA(className);
}

// Script::onAncestorChangedのWorkspace/System登録処理をバイパスする。
// ModuleScriptは自動実行の対象外で、require時にのみ実行されるため。
void ModuleScript::onAncestorChanged() {
    Instance::onAncestorChanged();
}

std::shared_ptr<Instance> ModuleScript::clone() const {
    auto copy = std::make_shared<ModuleScript>();
    copy->Name          = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "ModuleScript");
    // Path はclone時に再読込せず、元のソース／bytecode状態をそのまま復元する。
    copy->Source        = Source;
    copy->Path          = Path;
    copy->isPrecompiled = isPrecompiled;
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
