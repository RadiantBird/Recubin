#pragma once
#include <Instances/WorldGuiObject.hpp>
#include <Instances/Decal.hpp>

class SurfaceGui : public WorldGuiObject {
public:
    Face face = Face::Front;

    SurfaceGui();
    std::string GetClassName() override;
    bool IsA(std::string name) override;
    void setProperty(const std::string& name, const YAML::Node& val) override;
    std::shared_ptr<Instance> clone() const override;
};
