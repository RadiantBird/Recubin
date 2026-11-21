#pragma once
#include <Instances/Named.hpp>
#include <Instances/Instance.hpp>
#include <Util/Color4.hpp>

// BaseCube派生1体、またはModel（子孫の全BaseCubeを再帰対象化）の子として置く装飾インスタンス。
// 自身は空間的プロパティ(Position/Size等)を持たない（ParticleEmitterと同じ規約）。
// 親の形状を深度テスト無効・単色塗り+輪郭線で描画する（壁越しに見える）。
class Highlight : public Named<Highlight, Instance> {
public:
    static constexpr const char* ClassName = "Highlight";

    Color4 FillColor        = Color4(0.0f, 0.85f, 1.0f, 0.35f); // 半透明シアン
    Color4 OutlineColor     = Color4(0.0f, 0.85f, 1.0f, 1.0f);  // 不透明シアン
    float  OutlineThickness = 2.0f;  // glLineWidthに渡すpx幅
    bool   Enabled          = true;

    Highlight();
    bool IsA(std::string name) override;
    void setProperty(const std::string& name, const YAML::Node& val) override;
    std::shared_ptr<Instance> clone() const override;
};
