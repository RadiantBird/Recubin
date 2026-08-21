#include <Instances/PhysicalFileInstance.hpp>

#include <Core/PhysicalFileInstanceRegistry.hpp>
#include <Core/PropertyRegistry.hpp>

PhysicalFileInstance::PhysicalFileInstance(std::string className)
    : Instance(std::move(className)) {
    (void)PhysicalFileInstanceRegistry::types();
}

bool PhysicalFileInstance::IsA(std::string className) {
    if (className == "PhysicalFileInstance") return true;
    return Instance::IsA(std::move(className));
}

void PhysicalFileInstance::setProperty(const std::string& name,
                                       const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, getClassName(), name, value)) return;
    Instance::setProperty(name, value);
}

void PhysicalFileInstance::clonePhysicalFileFieldsTo(
    PhysicalFileInstance& copy, std::string_view className) const {
    PropertyRegistry::cloneFields(this, &copy, className);
    copy.Name = Name;
}
