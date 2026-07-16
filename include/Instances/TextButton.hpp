#pragma once
#include <Instances/GuiButton.hpp>
#include <Instances/Named.hpp>
#include <Instances/GuiContent.hpp>

class TextButton : public Named<TextButton, GuiButton> {
public:
    static constexpr const char* ClassName = "TextButton";

    TextContent m_text;
    TextContent* textContent() override { return &m_text; }

    TextButton();
    bool IsA(std::string name) override;
    void setProperty(const std::string& name, const YAML::Node& val) override;
    std::shared_ptr<Instance> clone() const override;
};
