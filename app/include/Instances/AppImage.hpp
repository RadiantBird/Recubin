#pragma once
#include <include/Instances/Instance.hpp>
#include <string>

class AppImage : public Instance {
public:
    std::string iconPath;

    AppImage();
    virtual ~AppImage() = default;

    virtual std::string getClassName() override { return "AppImage"; }
    virtual bool IsA(std::string name) override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    virtual std::shared_ptr<Instance> clone() const override;
};
