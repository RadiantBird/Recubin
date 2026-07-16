#include <Instances/ImageButton.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <Instances/GuiContentProps.hpp>

static const bool s_imageButtonRegistered = []{
    using namespace PropertyRegistry;
    registerClass("ImageButton", "GuiButton", {
        GuiContentProps::image<&ImageButton::m_image>().luaReadOnly(),
    });
    return true;
}();

ImageButton::ImageButton() : Named<ImageButton, GuiButton>("ImageButton") {}

bool ImageButton::IsA(std::string name) {
    if (name == "ImageButton") return true;
    return GuiButton::IsA(name);
}

void ImageButton::setProperty(const std::string& name, const YAML::Node& val) {
    if (PropertyRegistry::loadProperty(this, "ImageButton", name, val)) return;
    GuiButton::setProperty(name, val);
}

std::shared_ptr<Instance> ImageButton::clone() const {
    auto copy = std::make_shared<ImageButton>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "ImageButton");
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
