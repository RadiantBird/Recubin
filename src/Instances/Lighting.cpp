#include <include/Instances/Lighting.hpp>



Lighting::Lighting() : Instance("Lighting") {}

std::string Lighting::getClassName() { return "Lighting"; }

bool Lighting::IsA(std::string className) {
    if (className == "Lighting") return true;
    return Instance::IsA(className);
}

std::shared_ptr<Instance> Lighting::clone() const {
    auto copy = std::make_shared<Lighting>();
    copy->lightDir   = lightDir;
    copy->brightness = brightness;
    return copy;
}

void Lighting::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Direction") {
        lightDir.x = value[0].as<float>();
        lightDir.y = value[1].as<float>();
        lightDir.z = value[2].as<float>();
    } else if (name == "Brightness") {
        brightness = value.as<float>();
    } else {
        Instance::setProperty(name, value);
    }
}
