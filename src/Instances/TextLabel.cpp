#include <Instances/TextLabel.hpp>

TextLabel::TextLabel() : ScreenGuiObject("TextLabel") {}

std::string TextLabel::getClassName() { return "TextLabel"; }

bool TextLabel::IsA(std::string name) {
    if (name == "TextLabel") return true;
    return ScreenGuiObject::IsA(name);
}

void TextLabel::setProperty(const std::string& name, const YAML::Node& val) {
    if (name == "Text") {
        Text = val.as<std::string>();
    } else if (name == "TextColor") {
        TextColor = { val[0].as<float>(), val[1].as<float>(),
                      val[2].as<float>(), val[3].as<float>() };
    } else {
        ScreenGuiObject::setProperty(name, val);
    }
}

std::shared_ptr<Instance> TextLabel::clone() const {
    auto copy = std::make_shared<TextLabel>();
    copy->Name            = Name;
    copy->Active          = Active;
    copy->Position        = Position;
    copy->Size            = Size;
    copy->NormType        = NormType;
    copy->Visible         = Visible;
    copy->BackgroundColor = BackgroundColor;
    copy->ZIndex          = ZIndex;
    copy->Text            = Text;
    copy->TextColor       = TextColor;
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}
