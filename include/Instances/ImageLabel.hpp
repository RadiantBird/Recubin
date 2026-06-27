#pragma once
#include <Instances/ScreenGuiObject.hpp>
#include <Instances/Named.hpp>
#include <string>

class ImageLabel : public Named<ImageLabel, ScreenGuiObject> {
public:
    static constexpr const char* ClassName = "ImageLabel";

    std::string  imagePath;
    unsigned int m_textureID = 0;

    void        setImage(const std::string& path);
    std::string getImage() const { return imagePath; }

    ImageLabel();
    bool IsA(std::string name) override;
    void setProperty(const std::string& name, const YAML::Node& val) override;
    std::shared_ptr<Instance> clone() const override;
};
