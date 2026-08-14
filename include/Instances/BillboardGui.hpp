#pragma once
#include <Instances/WorldGuiObject.hpp>
#include <Instances/Named.hpp>
#include <Math/Vector3.hpp>

enum class BillboardMode { Parallel, Focus };
enum class BillboardSizeMode { Screen, World };

class BillboardGui : public Named<BillboardGui, WorldGuiObject> {
public:
    static constexpr const char* ClassName = "BillboardGui";

    BillboardMode Mode = BillboardMode::Parallel;
    BillboardSizeMode SizeMode = BillboardSizeMode::Screen;
    Vector3 Offset = {0.0f, 0.0f, 0.0f};

    BillboardGui();
    bool IsA(std::string name) override;
    void setProperty(const std::string& name, const YAML::Node& val) override;
    std::shared_ptr<Instance> clone() const override;
};
