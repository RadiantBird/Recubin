#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Instances/Named.hpp>
#include <include/Math/Vector2.hpp>
#include <include/Math/Vector3.hpp>
#include <memory>
#include <string>

class Cube;
class ParticleEmitter;
class PointLight;
class Sound;

// 天気システム。雲・雨・雪・風・雷・環境音を1クラスに統合する。
// Workspaceの直接の子として配置する想定（Sun/Moon/Skyboxと同じ探索方針）。
// 自身はSpatialではない（Lighting/Systemと同型）。子オブジェクト（雨/雪の発生源、
// 落雷用ライト/スパーク、環境音）はコンストラクタではなくupdate()初回呼び出し時に
// 遅延生成する（コンストラクタ内でのaddChild()はshared_from_this()がbad_weak_ptrで
// 失敗するため不可）。これらの子は非シリアライズ・clone()非対象で、毎回同じ構成に
// 再構築される。
enum class WeatherType { Clear = 0, Rain = 1, Snow = 2 };

class Weather : public Named<Weather, Instance> {
public:
    static constexpr const char* ClassName = "Weather";

    bool        Enabled          = true;
    WeatherType CurrentWeather   = WeatherType::Clear;
    float       CloudCover       = 0.5f;
    float       CloudDensity     = 0.6f;
    float       CloudHeight      = 300.0f;
    Vector3     WindDirection    = Vector3(0.0f, 0.0f, 0.0f);
    bool        LightningEnabled = true;
    float       LightningChance  = 0.05f;
    std::string ClearAmbientPath;
    std::string RainAmbientPath;
    std::string SnowAmbientPath;
    float       AmbientVolume    = 1.0f;

    Weather();

    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;

    // メインループから毎フレーム1回だけ呼ぶ（ParticleEmitter::updateAllより前に）
    void update(float dt, const Vector3& cameraPosition);

    // Rendererが読む雲UVスクロールオフセット（読み取り専用）
    Vector2 getCloudScrollOffset() const { return m_cloudScrollOffset; }

    // Workspace直下からWeatherを探してupdate()する（Sun/Moonと同じ直接の子スキャン）
    static void updateAll(Instance* workspaceRoot, float dt, const Vector3& cameraPosition);

private:
    bool m_childrenBuilt = false;
    std::shared_ptr<Cube>            m_skyAnchor;
    std::shared_ptr<Cube>            m_lightningAnchor;
    std::shared_ptr<ParticleEmitter> m_rainEmitter;
    std::shared_ptr<ParticleEmitter> m_snowEmitter;
    std::shared_ptr<PointLight>      m_lightningLight;
    std::shared_ptr<ParticleEmitter> m_lightningSparks;
    std::shared_ptr<Sound>           m_ambientSound;

    int     m_prevWeather = -1;
    float   m_lightningTimer = 0.0f;
    Vector2 m_cloudScrollOffset = Vector2(0.0f, 0.0f);

    void ensureChildren();
    void updateAmbientAudio();
    void attemptStrike();
};
