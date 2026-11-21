#include <include/Instances/MaterialService.hpp>

MaterialService::MaterialService() : Instance("MaterialService") {}

std::string MaterialService::getClassName() { return "MaterialService"; }

bool MaterialService::IsA(std::string className) {
    if (className == "MaterialService") return true;
    return Instance::IsA(className);
}

std::shared_ptr<Instance> MaterialService::clone() const {
    auto copy = std::make_shared<MaterialService>();
    copy->Name = Name;
    for (const auto& [name, child] : children) {
        (void)name;
        copy->addChild(child->clone());
    }
    return copy;
}
