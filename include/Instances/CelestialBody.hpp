#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Util/Color4.hpp>
#include <string>
#include <string_view>
#include <vector>

struct PropertyDesc;

// ==================================================================
//  CelestialBody
//
//  Sun / Moon の共通基底。空に見える円盤だけを担う軽量Instance（Spatialでも
//  BaseCubeでもない。物理・Material参照・ギズモの対象にならない）。
//  Rendererが毎フレームカメラ基準で SKY_RENDER_DISTANCE の位置へ描く。
//
//  Distance（見かけの距離）: 円盤の見かけの大きさを決める。描画位置は常に一定距離に
//  置き、直径を baseDiameter() × SKY_RENDER_DISTANCE / Distance にする。距離が
//  大きいほど角度的に小さく見える。Distance = DEFAULT_DISTANCE が従来の見た目。
//
//  逆光（グロー＋ベール）と光条（回折スパイク）: 円盤の周りと画面に加算で重ねる
//  オーバーレイ効果（sky_flare シェーダー）。強さが0の効果は描かず、既定はすべて無効。
//  オブジェクトに遮られても被さる（縁のにじみも出る）。
// ==================================================================
class CelestialBody : public Instance {
public:
    // カメラからの描画距離。Skybox(2500stud〜)より手前に収まる値。
    static constexpr float SKY_RENDER_DISTANCE = 1000.0f;
    static constexpr float DEFAULT_DISTANCE = 1000.0f;
    static constexpr float MIN_DISTANCE = 10.0f;
    static constexpr float MAX_DISTANCE = 100000.0f;

    Color4 Color;
    float Distance = DEFAULT_DISTANCE;

    // ---- 逆光（Backlight）----
    // グロー: 円盤の周りの柔らかい光のにじみ。GlowRadius は円盤の半径の何倍まで広がるか。
    float GlowIntensity = 0.0f;
    float GlowRadius = 6.0f;
    // ベール: カメラがこの天体の方向を向くほど強まる、画面全体の白い霞（コントラスト低下）。
    // VeilFalloff が大きいほど天体の方向のごく近くだけに効く。
    float VeilIntensity = 0.0f;
    float VeilFalloff = 6.0f;

    // ---- 光条（Spikes）----
    // 回折スパイク: 円盤から放射状に伸びる細い光の筋（カメラの絞り由来の星形）。
    // SpikeCount は筋の本数（4 = 十字、6/8 = 星形）、SpikeLength は円盤の半径の何倍の長さか、
    // SpikeRotation は度。
    float SpikeIntensity = 0.0f;
    int SpikeCount = 4;
    float SpikeLength = 12.0f;
    float SpikeRotation = 0.0f;

    // 真なら、天体が地平線の下に沈むにつれて上記の効果を消す（地面の向こうの太陽の
    // グレアが地面越しに見えないように）。宇宙のように地平線が無いシーンでは偽にする。
    bool HorizonFade = true;

    CelestialBody(std::string name, const Color4& color);
    virtual ~CelestialBody() = default;

    // Distance = DEFAULT_DISTANCE のときの円盤の直径(stud)
    virtual float baseDiameter() const = 0;

    // 実際に描く直径(stud)。Distanceが不正/範囲外のときは範囲内へ丸める。
    float apparentDiameter() const;

    // 逆光または光条の効果が1つでも有効か（強さが0より大きいか）。
    bool hasFlareEffect() const;

    // Sun/Moon共通のスキーマ（Color, Distance, 逆光, 光条）を先頭プロパティ leading の後ろに
    // 続けて className へ登録する。
    static void registerSchema(std::string_view className, std::vector<PropertyDesc> leading);

    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;

protected:
    // 旧形式（BaseCube派生だったSun/Moon）のYAML互換。不要になったBaseCube系プロパティを
    // 無視し、旧Sizeは見かけの大きさを保つDistanceへ換算する。
    bool loadLegacyProperty(const std::string& name, const YAML::Node& value);

private:
    bool m_loggedLegacyProperties = false;
};
