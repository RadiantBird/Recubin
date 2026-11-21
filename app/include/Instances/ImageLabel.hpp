#pragma once
#include <Instances/ScreenGuiObject.hpp>
#include <Instances/Named.hpp>
#include <Instances/GuiContent.hpp>
#include <string>

class ImageLabel : public Named<ImageLabel, ScreenGuiObject> {
public:
    static constexpr const char* ClassName = "ImageLabel";

    ImageContent m_image;
    ImageContent* imageContent() override { return &m_image; }

    ImageLabel();
    bool IsA(std::string name) override;
    void setProperty(const std::string& name, const YAML::Node& val) override;
    std::shared_ptr<Instance> clone() const override;
};
