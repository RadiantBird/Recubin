#pragma once

#include <include/Instances/Cube.hpp>
#include <include/Instances/Named.hpp>

// Characterの出現位置。描画と物理形状は通常のCubeをそのまま利用する。
class SpawnLocation : public Named<SpawnLocation, Cube> {
public:
    static constexpr const char* ClassName = "SpawnLocation";

    bool Enabled = true;

    explicit SpawnLocation(Vector3 position = Vector3(0, 0, 0));

    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
};
