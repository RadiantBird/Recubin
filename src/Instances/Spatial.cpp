#include "include/Instances/Spatial.hpp"

bool Spatial::IsA(std::string className) {
    if (className == "Spatial") {
        return true;
    }
    return Instance::IsA(className);
}

void Spatial::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Position") {
        this->Position = value.as<Vector3>();
    } else if (name == "Size") {
        this->Size = value.as<Vector3>();
    } else {
        Instance::setProperty(name, value);
    }
}
