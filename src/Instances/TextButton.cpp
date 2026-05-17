#include <Instances/TextButton.hpp>

TextButton::TextButton() : GuiButton("TextButton") {}

std::string TextButton::GetClassName() { return "TextButton"; }

bool TextButton::IsA(std::string name) {
    if (name == "TextButton") return true;
    return GuiButton::IsA(name);
}

void TextButton::setProperty(const std::string& name, const YAML::Node& val) {
    if (name == "Text") {
        Text = val.as<std::string>();
    } else if (name == "TextColor") {
        TextColor = { val[0].as<float>(), val[1].as<float>(),
                      val[2].as<float>(), val[3].as<float>() };
    } else {
        GuiButton::setProperty(name, val);
    }
}

std::shared_ptr<Instance> TextButton::clone() const {
    auto copy = std::make_shared<TextButton>();
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
