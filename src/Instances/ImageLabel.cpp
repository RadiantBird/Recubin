#include <Instances/ImageLabel.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <include/Core/Renderer.hpp>

static const bool s_imageLabelRegistered = []{
    using namespace PropertyRegistry;
    registerClass("ImageLabel", "ScreenGuiObject", {
        method_prop<&ImageLabel::getImage, &ImageLabel::setImage>("Image"),
    });
    return true;
}();

ImageLabel::ImageLabel() : Named<ImageLabel, ScreenGuiObject>("ImageLabel") {}

void ImageLabel::setImage(const std::string& path) {
    imagePath = path;
    if (!path.empty() && Renderer::instance)
        m_textureID = Renderer::instance->loadTexture(path.c_str());
}

bool ImageLabel::IsA(std::string name) {
    if (name == "ImageLabel") return true;
    return ScreenGuiObject::IsA(name);
}

void ImageLabel::setProperty(const std::string& name, const YAML::Node& val) {
    if (PropertyRegistry::loadProperty(this, "ImageLabel", name, val)) return;
    ScreenGuiObject::setProperty(name, val);
}

std::shared_ptr<Instance> ImageLabel::clone() const {
    auto copy = std::make_shared<ImageLabel>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "ImageLabel");
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
