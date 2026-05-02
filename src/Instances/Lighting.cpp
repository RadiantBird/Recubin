#include <include/Instances/Lighting.hpp>

static const char* s_faceKeys[] = {
    "SkyboxRight", "SkyboxLeft", "SkyboxTop",
    "SkyboxBottom", "SkyboxFront", "SkyboxBack"
};

Lighting::Lighting() : Instance("Lighting") {}

std::string Lighting::GetClassName() { return "Lighting"; }

bool Lighting::IsA(std::string className) {
    if (className == "Lighting") return true;
    return Instance::IsA(className);
}

std::shared_ptr<Instance> Lighting::clone() const {
    auto copy = std::make_shared<Lighting>();
    copy->lightDir   = lightDir;
    copy->brightness = brightness;
    copy->skyboxDirty = false;
    for (int i = 0; i < 6; i++) {
        copy->skyboxPaths[i] = skyboxPaths[i];
        if (!skyboxPaths[i].empty()) {
            copy->skyboxDirty = true;
        }
    }
    return copy;
}

void Lighting::setSkyboxPath(int faceIndex, const std::string& path) {
    if (faceIndex < 0 || faceIndex >= 6) return;
    skyboxPaths[faceIndex] = path;
    skyboxDirty = true;
}

void Lighting::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Direction") {
        lightDir.x = value[0].as<float>();
        lightDir.y = value[1].as<float>();
        lightDir.z = value[2].as<float>();
    } else if (name == "Brightness") {
        brightness = value.as<float>();
    } else {
        for (int i = 0; i < 6; i++) {
            if (name == s_faceKeys[i]) {
                setSkyboxPath(i, value.as<std::string>());
                return;
            }
        }
        Instance::setProperty(name, value);
    }
}
