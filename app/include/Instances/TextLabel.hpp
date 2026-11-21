#pragma once
#include <Instances/ScreenGuiObject.hpp>
#include <Instances/Named.hpp>
#include <Instances/GuiContent.hpp>

class TextLabel : public Named<TextLabel, ScreenGuiObject> {
public:
    static constexpr const char* ClassName = "TextLabel";

    TextContent m_text;
    TextContent* textContent() override { return &m_text; }

    TextLabel();
    bool IsA(std::string name) override;
    void setProperty(const std::string& name, const YAML::Node& val) override;
    std::shared_ptr<Instance> clone() const override;
};
