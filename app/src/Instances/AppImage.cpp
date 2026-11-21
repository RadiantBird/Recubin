#include <Instances/AppImage.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_appImageRegistered = []{
    using namespace PropertyRegistry;
    registerClass("AppImage", {
        field<&AppImage::iconPath>("IconPath").omitEmpty(),  // 空なら YAML へ出力しない（既存挙動）
    });
    return true;
}();

AppImage::AppImage() : Instance("AppImage") {}

bool AppImage::IsA(std::string name) {
    if (name == "AppImage") return true;
    return Instance::IsA(name);
}

void AppImage::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "AppImage", name, value)) return;
    Instance::setProperty(name, value);
}

std::shared_ptr<Instance> AppImage::clone() const {
    auto copy = std::make_shared<AppImage>();
    PropertyRegistry::cloneFields(this, copy.get(), "AppImage");
    return copy;
}
