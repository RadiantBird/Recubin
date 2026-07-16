#include <Instances/TextLabel.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <Instances/GuiContentProps.hpp>

static const bool s_textLabelRegistered = []{
    using namespace PropertyRegistry;
    registerClass("TextLabel", "ScreenGuiObject", {
        GuiContentProps::text     <&TextLabel::m_text>(),
        GuiContentProps::textColor<&TextLabel::m_text>(),
        field<&ScreenGuiObject::FontSize>("FontSize", 0, 200, 1),  // 0 = 既定サイズ
    });
    return true;
}();

TextLabel::TextLabel() : Named<TextLabel, ScreenGuiObject>("TextLabel") {}

bool TextLabel::IsA(std::string name) {
    if (name == "TextLabel") return true;
    return ScreenGuiObject::IsA(name);
}

void TextLabel::setProperty(const std::string& name, const YAML::Node& val) {
    if (PropertyRegistry::loadProperty(this, "TextLabel", name, val)) return;
    ScreenGuiObject::setProperty(name, val);
}

std::shared_ptr<Instance> TextLabel::clone() const {
    auto copy = std::make_shared<TextLabel>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "TextLabel");  // 基底 ScreenGuiObject 分も集約
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
