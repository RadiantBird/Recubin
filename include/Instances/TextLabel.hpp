#pragma once
#include <Instances/ScreenGuiObject.hpp>
#include <Instances/Named.hpp>

class TextLabel : public Named<TextLabel, ScreenGuiObject> {
public:
    static constexpr const char* ClassName = "TextLabel";

    std::string Text;
    Color4      TextColor = {0.f, 0.f, 0.f, 1.f};

    TextLabel();
    bool IsA(std::string name) override;
    void setProperty(const std::string& name, const YAML::Node& val) override;
    std::shared_ptr<Instance> clone() const override;
};
