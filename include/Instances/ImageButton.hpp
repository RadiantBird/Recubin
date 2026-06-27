#pragma once
#include <Instances/GuiButton.hpp>
#include <Instances/Named.hpp>
#include <string>

class ImageButton : public Named<ImageButton, GuiButton> {
public:
    static constexpr const char* ClassName = "ImageButton";

    std::string  imagePath;
    unsigned int m_textureID = 0;

    void        setImage(const std::string& path);
    std::string getImage() const { return imagePath; }

    ImageButton();
    bool IsA(std::string name) override;
    void setProperty(const std::string& name, const YAML::Node& val) override;
    std::shared_ptr<Instance> clone() const override;
};
