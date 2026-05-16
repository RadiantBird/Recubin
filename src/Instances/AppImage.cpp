#include <Instances/AppImage.hpp>

AppImage::AppImage() : Instance("AppImage") {}

bool AppImage::IsA(std::string name) {
    if (name == "AppImage") return true;
    return Instance::IsA(name);
}

void AppImage::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "IconPath") {
        iconPath = value.as<std::string>();
    } else {
        Instance::setProperty(name, value);
    }
}

std::shared_ptr<Instance> AppImage::clone() const {
    auto copy = std::make_shared<AppImage>();
    copy->iconPath = iconPath;
    return copy;
}
