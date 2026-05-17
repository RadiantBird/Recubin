#pragma once
#include <Instances/GuiButton.hpp>

class TextButton : public GuiButton {
public:
    std::string Text;
    Color4      TextColor = {0.f, 0.f, 0.f, 1.f};

    TextButton();
    std::string GetClassName() override;
    bool IsA(std::string name) override;
    void setProperty(const std::string& name, const YAML::Node& val) override;
    std::shared_ptr<Instance> clone() const override;
};
