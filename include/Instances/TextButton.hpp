#pragma once
#include <Instances/GuiButton.hpp>
#include <Instances/Named.hpp>

class TextButton : public Named<TextButton, GuiButton> {
public:
    static constexpr const char* ClassName = "TextButton";

    std::string Text;
    Color4      TextColor = {0.f, 0.f, 0.f, 1.f};

    TextButton();
    bool IsA(std::string name) override;
    void setProperty(const std::string& name, const YAML::Node& val) override;
    std::shared_ptr<Instance> clone() const override;
};
