#pragma once
#include <Instances/Instance.hpp>
#include <string>

class FontFile : public Instance {
public:
    std::string Path;

    FontFile();
    virtual ~FontFile() = default;

    std::string getClassName() override { return "FontFile"; }
    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
};
