#include "include/Instances/Spatial.hpp"

bool Spatial::IsA(std::string className) {
    if (className == "Spatial") {
        return true;
    }
    return Instance::IsA(className);
}

void Spatial::setProperty(const std::string& name, const YAML::Node& value) {
if (name == "Position" || name == "Size") {
        // YAMLが [x, y, z] の形式であることを前提に直接読み込む
        if (value.IsSequence() && value.size() == 3) {
            Vector3 vec;
            vec.x = value[0].as<float>();
            vec.y = value[1].as<float>();
            vec.z = value[2].as<float>();

            if (name == "Position") {
                this->Position = vec;
            } else {
                this->Size = vec;
            }
        }
    } else {
        Instance::setProperty(name, value);
    }
}
