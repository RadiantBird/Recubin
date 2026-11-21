#pragma once
#include <Instances/GuiButton.hpp>
#include <Instances/Named.hpp>
#include <Instances/GuiContent.hpp>
#include <string>

class ImageButton : public Named<ImageButton, GuiButton> {
public:
    static constexpr const char* ClassName = "ImageButton";

    ImageContent m_image;
    ImageContent* imageContent() override { return &m_image; }

    ImageButton();
    bool IsA(std::string name) override;
    void setProperty(const std::string& name, const YAML::Node& val) override;
    std::shared_ptr<Instance> clone() const override;
};
