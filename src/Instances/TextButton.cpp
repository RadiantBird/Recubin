#include <Instances/TextButton.hpp>
#include <include/Core/PropertyRegistry.hpp>

static const bool s_textButtonRegistered = []{
    using namespace PropertyRegistry;
    registerClass("TextButton", "GuiButton", {
        field<&TextButton::Text>     ("Text"),
        field<&TextButton::TextColor>("TextColor"),
        field<&ScreenGuiObject::FontSize>("FontSize", 0, 200, 1),  // 0 = 既定サイズ
    });
    return true;
}();

TextButton::TextButton() : Named<TextButton, GuiButton>("TextButton") {}

bool TextButton::IsA(std::string name) {
    if (name == "TextButton") return true;
    return GuiButton::IsA(name);
}

void TextButton::setProperty(const std::string& name, const YAML::Node& val) {
    if (PropertyRegistry::loadProperty(this, "TextButton", name, val)) return;
    GuiButton::setProperty(name, val);
}

std::shared_ptr<Instance> TextButton::clone() const {
    auto copy = std::make_shared<TextButton>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "TextButton");  // GuiButton→ScreenGuiObject 分も集約
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
