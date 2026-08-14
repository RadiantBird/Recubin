#pragma once
#include <include/Instances/BaseCube.hpp>
#include <include/Instances/Named.hpp>

// 水などの液体ボリューム。コリジョンを持たず、侵入した BaseCube に浮力を与える。
class LiquidCube : public Named<LiquidCube, BaseCube> {
public:
    static constexpr const char* ClassName = "LiquidCube";
    static constexpr float WAVE_ANGULAR_SPEED = 1.5f;
    static constexpr float WAVE_SPATIAL_FREQUENCY = 4.0f;
    static constexpr float WAVE_AMPLITUDE = 0.06f;

    static float waveHeight(float normalizedX, float normalizedZ, float time);

    float Density = 1.0f;   // 流体密度（浮力係数）。物体側の暗黙密度は1.0基準
                             // (Physics::createActor)。Density<1で沈み、>1で浮く目安。

    LiquidCube(Vector3 Pos, Vector3 Sz);

    void draw(int modelLoc, int shaderProgram);

    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;
};
