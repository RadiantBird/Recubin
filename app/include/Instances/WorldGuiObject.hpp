#pragma once
#include <Instances/GuiObject.hpp>

class WorldGuiObject : public GuiObject {
public:
    explicit WorldGuiObject(std::string className);
    virtual bool IsA(std::string name) override;
    virtual void setProperty(const std::string& name, const YAML::Node& val) override;
};
