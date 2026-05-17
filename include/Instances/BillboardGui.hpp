#pragma once
#include <Instances/WorldGuiObject.hpp>

enum class BillboardMode { Parallel, Focus };

class BillboardGui : public WorldGuiObject {
public:
    BillboardMode Mode = BillboardMode::Parallel;

    BillboardGui();
    std::string GetClassName() override;
    bool IsA(std::string name) override;
    void setProperty(const std::string& name, const YAML::Node& val) override;
    std::shared_ptr<Instance> clone() const override;
};
