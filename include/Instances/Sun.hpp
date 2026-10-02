#pragma once

#include <include/Instances/CelestialBody.hpp>
#include <include/Instances/Named.hpp>
#include <include/Math/Vector2.hpp>
#include <include/Math/Vector3.hpp>

// 太陽。空に見える円盤であり、同時に**平行光源の向きの唯一の正**でもある
// （Workspaceに最初のSunがあれば、その向きで光と影を作る。無ければ平行光源なし）。
//
// Angle = Vector2(方角, 高度)（度）。方角は北0°・東90°の時計回り（spec: +Xが北、+Zが東）、
// 高度は水平0°・天頂90°・負で地平線の下。
class Sun : public Named<Sun, CelestialBody> {
public:
    static constexpr const char* ClassName = "Sun";
    static constexpr float BASE_DIAMETER = 200.0f;

    Vector2 Angle = defaultAngle();

    Sun();

    float baseDiameter() const override { return BASE_DIAMETER; }
    void setAngle(const Vector2& angle);

    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;

    // 旧 Lighting.Direction の既定 (1,-1,-1) が指していた太陽の位置。既定の見た目を従来と揃える。
    static Vector2 defaultAngle();

    // 太陽の方向（単位ベクトル）。+Xが北、+Zが東。方角・高度は範囲外でも正規化して扱う。
    static Vector3 directionFromAngle(const Vector2& angle);

    // 光の進行方向（太陽方向の逆）。従来の Lighting.Direction と同じ向きの規約。
    static Vector3 lightDirectionFromAngle(const Vector2& angle);

    // 方向ベクトルから (方角[0,360), 高度[-90,90]) を求める。零ベクトルは既定角を返す。
    static Vector2 angleFromDirection(const Vector3& direction);

    // 旧スカラーAngle（方向 (0, sin a, cos a)）をVector2へ換算する。
    static Vector2 angleFromLegacyScalar(float degrees);
};
