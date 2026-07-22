#include <include/Instances/Weather.hpp>
#include <include/Instances/Cube.hpp>
#include <include/Instances/ParticleEmitter.hpp>
#include <include/Instances/PointLight.hpp>
#include <include/Instances/Sound.hpp>
#include <include/Instances/BaseCube.hpp>
#include <include/Instances/Workspace.hpp>
#include <include/Core/Physics.hpp>
#include <include/Core/AudioService.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <include/Util/Color4.hpp>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <vector>

namespace {
    constexpr float kLightningFlashDuration = 0.2f;
    constexpr float kLightningBrightness    = 10.0f;
    constexpr float kSkyAnchorHeight        = 150.0f;
    // Keep cloud motion visible at the default wind strength (10 studs/sec).
    constexpr float kWindToUVScale          = 0.002f;
    constexpr float kBoltHeight             = 80.0f;  // 落雷対象頭上から雷柱を伸ばす高さ
    constexpr int   kBoltIterations         = 5;       // 中点変位法の反復回数（頂点数は2^n+1）
    constexpr float kBoltJitter             = 6.0f;    // 初期の水平ジッター振幅（反復ごとに半減）

    // 中点変位法で開始点→終了点の間をジグザグに折れ曲がる頂点列にする
    std::vector<Vector3> generateBoltPoints(const Vector3& start, const Vector3& end) {
        auto frand11 = []() { return (static_cast<float>(std::rand()) / RAND_MAX) * 2.0f - 1.0f; };
        std::vector<Vector3> points{ start, end };
        float jitter = kBoltJitter;
        for (int iter = 0; iter < kBoltIterations; ++iter) {
            std::vector<Vector3> next;
            next.reserve(points.size() * 2);
            next.push_back(points.front());
            for (size_t i = 0; i + 1 < points.size(); ++i) {
                Vector3 a = points[i];
                Vector3 b = points[i + 1];
                Vector3 mid = (a + b) * 0.5f;
                mid.x += frand11() * jitter;
                mid.z += frand11() * jitter;
                next.push_back(mid);
                next.push_back(b);
            }
            points = std::move(next);
            jitter *= 0.5f;
        }
        return points;
    }
}

static const bool s_weatherRegistered = []{
    using namespace PropertyRegistry;
    registerClass("Weather", {
        field<&Weather::Enabled>("Enabled"),
        enumProp<&Weather::CurrentWeather>("CurrentWeather",
            {{"Clear", 0}, {"Rain", 1}, {"Snow", 2}}, /*yamlAsString*/true),
        field<&Weather::CloudCover>      ("CloudCover",       0.0f, 1.0f,    0.01f).clampLua(),
        field<&Weather::CloudDensity>    ("CloudDensity",     0.0f, 1.0f,    0.01f).clampLua(),
        field<&Weather::CloudHeight>     ("CloudHeight",      50.0f, 2000.0f, 5.0f).clampLua(),
        field<&Weather::WindDirection>   ("WindDirection"),
        field<&Weather::LightningEnabled>("LightningEnabled"),
        field<&Weather::LightningInterval>("LightningInterval", 1.0f, 120.0f, 1.0f).clampLua(),
        field<&Weather::ClearAmbientPath>("ClearAmbientPath").omitEmpty(),
        field<&Weather::RainAmbientPath> ("RainAmbientPath").omitEmpty(),
        field<&Weather::SnowAmbientPath> ("SnowAmbientPath").omitEmpty(),
        field<&Weather::AmbientVolume>   ("AmbientVolume",    0.0f, 1.0f,    0.05f).clampLua(),
    });
    return true;
}();

Weather::Weather() : Named<Weather, Instance>("Weather") {}

bool Weather::IsA(std::string className) {
    if (className == "Weather") return true;
    return Instance::IsA(className);
}

void Weather::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "Weather", name, value)) return;
    Instance::setProperty(name, value);
}

std::shared_ptr<Instance> Weather::clone() const {
    auto copy = std::make_shared<Weather>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "Weather");
    // 子(雨/雪/雷/環境音)はコピーしない。copyのensureChildren()が初回update()で再構築する。
    return copy;
}

void Weather::ensureChildren() {
    if (m_childrenBuilt) return;

    // シーンから既にロードされた子は採用し、無いものだけ既定値で生成する（ユーザー編集の永続化）
    auto adopt = [](Instance* parent, const char* name, const char* className) -> std::shared_ptr<Instance> {
        auto const& kids = parent->getChildren();
        auto it = kids.find(name);
        if (it != kids.end() && it->second->IsA(className)) return it->second;
        return nullptr;
    };

    auto makeAnchor = [](const char* name) {
        auto cube = std::make_shared<Cube>(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.1f, 0.1f, 0.1f), Cube::defaultTextureID);
        cube->Name = name;
        cube->Anchored   = true;
        cube->CanCollide = false;
        cube->CastShadow = false;
        cube->Color      = Color4(0.0f, 0.0f, 0.0f, 0.0f);
        return cube;
    };

    if (auto existing = adopt(this, "WeatherSkyAnchor", "Cube")) {
        m_skyAnchor = std::static_pointer_cast<Cube>(existing);
    } else {
        m_skyAnchor = makeAnchor("WeatherSkyAnchor");
        addChild(m_skyAnchor);
    }

    if (auto existing = adopt(this, "WeatherLightningAnchor", "Cube")) {
        m_lightningAnchor = std::static_pointer_cast<Cube>(existing);
    } else {
        m_lightningAnchor = makeAnchor("WeatherLightningAnchor");
        addChild(m_lightningAnchor);
    }

    if (auto existing = adopt(m_skyAnchor.get(), "RainEmitter", "ParticleEmitter")) {
        m_rainEmitter = std::static_pointer_cast<ParticleEmitter>(existing);
    } else {
    m_rainEmitter = std::make_shared<ParticleEmitter>();
    m_rainEmitter->Name             = "RainEmitter";
    m_rainEmitter->StartColor       = Color4(0.6f, 0.7f, 0.9f, 0.6f);
    m_rainEmitter->EndColor         = Color4(0.6f, 0.7f, 0.9f, 0.0f);
    m_rainEmitter->StartSize        = 0.15f;
    m_rainEmitter->EndSize          = 0.1f;
    m_rainEmitter->EmitRate         = 300.0f;
    m_rainEmitter->MaxParticles     = 800;
    m_rainEmitter->Lifetime         = 3.0f;
    m_rainEmitter->LifetimeVariance = 0.5f;
    m_rainEmitter->Speed            = 60.0f;
    m_rainEmitter->SpeedVariance    = 5.0f;
    m_rainEmitter->Direction        = Vector3(0.0f, -1.0f, 0.0f);
    m_rainEmitter->SpreadAngle      = 2.0f;
    m_rainEmitter->GravityScale     = 1.0f;
    m_rainEmitter->WindScale        = 1.0f;
    m_rainEmitter->SpawnRadius      = 80.0f;
    m_rainEmitter->CollisionCutoff  = true;
    m_rainEmitter->Enabled          = false;
    m_skyAnchor->addChild(m_rainEmitter);
    }

    if (auto existing = adopt(m_skyAnchor.get(), "SnowEmitter", "ParticleEmitter")) {
        m_snowEmitter = std::static_pointer_cast<ParticleEmitter>(existing);
    } else {
    m_snowEmitter = std::make_shared<ParticleEmitter>();
    m_snowEmitter->Name              = "SnowEmitter";
    m_snowEmitter->StartColor        = Color4(1.0f, 1.0f, 1.0f, 0.85f);
    m_snowEmitter->EndColor          = Color4(1.0f, 1.0f, 1.0f, 0.0f);
    m_snowEmitter->StartSize         = 0.25f;
    m_snowEmitter->EndSize           = 0.2f;
    m_snowEmitter->EmitRate          = 150.0f;
    m_snowEmitter->MaxParticles      = 600;
    m_snowEmitter->Lifetime          = 6.0f;
    m_snowEmitter->LifetimeVariance  = 1.0f;
    m_snowEmitter->Speed             = 4.0f;
    m_snowEmitter->SpeedVariance     = 1.0f;
    m_snowEmitter->Direction         = Vector3(0.0f, -1.0f, 0.0f);
    m_snowEmitter->SpreadAngle       = 20.0f;
    m_snowEmitter->GravityScale      = 0.15f;
    m_snowEmitter->WindScale         = 1.0f;
    m_snowEmitter->SpawnRadius       = 80.0f;
    m_snowEmitter->CollisionCutoff   = true;
    m_snowEmitter->SpinSpeed         = 30.0f;
    m_snowEmitter->SpinSpeedVariance = 20.0f;
    m_snowEmitter->Enabled           = false;
    m_skyAnchor->addChild(m_snowEmitter);
    }

    if (auto existing = adopt(m_lightningAnchor.get(), "LightningFlash", "PointLight")) {
        m_lightningLight = std::static_pointer_cast<PointLight>(existing);
    } else {
    m_lightningLight = std::make_shared<PointLight>();
    m_lightningLight->Name       = "LightningFlash";
    m_lightningLight->brightness = 0.0f;
    m_lightningLight->range      = 200.0f;
    m_lightningLight->lightColor = Color4(0.85f, 0.9f, 1.0f, 1.0f);
    m_lightningAnchor->addChild(m_lightningLight);
    }

    if (auto existing = adopt(m_lightningAnchor.get(), "LightningSparks", "ParticleEmitter")) {
        m_lightningSparks = std::static_pointer_cast<ParticleEmitter>(existing);
    } else {
    m_lightningSparks = std::make_shared<ParticleEmitter>();
    m_lightningSparks->Name         = "LightningSparks";
    m_lightningSparks->StartColor   = Color4(1.0f, 0.95f, 0.6f, 1.0f);
    m_lightningSparks->EndColor     = Color4(1.0f, 0.6f, 0.2f, 0.0f);
    m_lightningSparks->StartSize    = 0.3f;
    m_lightningSparks->EndSize      = 0.05f;
    m_lightningSparks->EmitRate     = 0.0f;
    m_lightningSparks->MaxParticles = 60;
    m_lightningSparks->Lifetime     = 0.6f;
    m_lightningSparks->LifetimeVariance = 0.2f;
    m_lightningSparks->Speed        = 15.0f;
    m_lightningSparks->SpeedVariance = 8.0f;
    m_lightningSparks->Direction    = Vector3(0.0f, 1.0f, 0.0f);
    m_lightningSparks->SpreadAngle  = 180.0f;
    m_lightningSparks->GravityScale = 1.5f;
    m_lightningSparks->Enabled      = false;
    m_lightningAnchor->addChild(m_lightningSparks);
    }

    if (auto existing = adopt(this, "WeatherAmbient", "Sound")) {
        m_ambientSound = std::static_pointer_cast<Sound>(existing);
        m_ambientSound->setLooping(true);
    } else if (AudioService::instance) {
        m_ambientSound = std::make_shared<Sound>(*AudioService::instance);
        m_ambientSound->Name = "WeatherAmbient";
        m_ambientSound->setLooping(true);
        addChild(m_ambientSound);
    }

    m_childrenBuilt = true;
}

void Weather::updateAmbientAudio() {
    if (!m_ambientSound) return;
    std::string path;
    switch (CurrentWeather) {
        case WeatherType::Clear: path = ClearAmbientPath; break;
        case WeatherType::Rain:  path = RainAmbientPath;  break;
        case WeatherType::Snow:  path = SnowAmbientPath;  break;
    }
    if (path == m_lastAmbientPath) return;
    m_lastAmbientPath = path;

    m_ambientSound->stop();
    if (!path.empty()) {
        m_ambientSound->loadFromFile(path);
        m_ambientSound->setLooping(true);
        m_ambientSound->setVolume(AmbientVolume);
        m_ambientSound->play();
    }
}

void Weather::attemptStrike() {
    Instance* wsInst = findFirstAncestorWorkspace();
    if (!wsInst) return;
    Workspace* ws = static_cast<Workspace*>(wsInst);
    Physics* physics = ws->getPhysicsEngine();
    if (!physics) return;

    std::vector<BaseCube*> candidates;
    std::vector<float> weights;
    auto collect = [&](auto& self, Instance* inst) -> void {
        if (!inst) return;
        if (inst->IsA("BaseCube")) {
            BaseCube* bc = static_cast<BaseCube*>(inst);
            if (bc->material.type == MaterialType::Metal && bc->Color.a > 0.001f) {
                candidates.push_back(bc);
                float h = std::max(bc->getWorldPosition().y, 0.0f) + 1.0f;
                weights.push_back(h * h);
            }
        }
        for (auto const& [name, child] : inst->getChildren())
            self(self, child.get());
    };
    for (auto const& [name, child] : ws->getChildren())
        collect(collect, child.get());

    if (candidates.empty()) return;

    float total = 0.0f;
    for (float w : weights) total += w;
    if (total <= 0.0f) return;

    auto frand01 = []() { return static_cast<float>(std::rand()) / RAND_MAX; };
    float r = frand01() * total;
    size_t idx = 0;
    float acc = 0.0f;
    for (; idx < weights.size(); ++idx) {
        acc += weights[idx];
        if (acc >= r) break;
    }
    if (idx >= candidates.size()) idx = candidates.size() - 1;
    BaseCube* target = candidates[idx];

    Vector3 top = target->getWorldPosition() + Vector3(0.0f, target->Size.y * 0.5f, 0.0f);
    Vector3 skyPoint = top + Vector3(0.0f, 500.0f, 0.0f);
    RaycastHit hit;
    bool didHit = physics->raycast(skyPoint, Vector3(0.0f, -1.0f, 0.0f), 500.0f + target->Size.y, hit, nullptr);
    if (!didHit || !hit.hit || hit.instance != static_cast<Instance*>(target)) return;

    if (m_lightningAnchor) m_lightningAnchor->teleportTo(top);
    if (m_lightningLight)  m_lightningLight->brightness = kLightningBrightness;
    m_lightningTimer = kLightningFlashDuration;
    m_lightningBoltPoints = generateBoltPoints(top + Vector3(0.0f, kBoltHeight, 0.0f), top);
    if (m_lightningSparks) m_lightningSparks->emit(40);
}

void Weather::update(float dt, const Vector3& cameraPosition) {
    ensureChildren();
    if (!Enabled) return;

    if (m_skyAnchor) {
        m_skyAnchor->teleportTo(Vector3(cameraPosition.x, cameraPosition.y + kSkyAnchorHeight, cameraPosition.z));
    }
    if (m_rainEmitter) m_rainEmitter->Enabled = (CurrentWeather == WeatherType::Rain);
    if (m_snowEmitter) m_snowEmitter->Enabled = (CurrentWeather == WeatherType::Snow);

    if (Instance* wsInst = findFirstAncestorWorkspace()) {
        static_cast<Workspace*>(wsInst)->Wind = WindDirection;
    }

    m_cloudScrollOffset = m_cloudScrollOffset + Vector2(WindDirection.x, WindDirection.z) * (kWindToUVScale * dt);

    updateAmbientAudio();

    if (CurrentWeather == WeatherType::Rain && LightningEnabled) {
        auto frand01 = []() { return static_cast<float>(std::rand()) / RAND_MAX; };
        float rate = 1.0f / std::max(LightningInterval, 0.01f); // 平均発生回数/秒
        float prob = 1.0f - std::exp(-rate * dt);
        if (frand01() < prob) attemptStrike();
    }

    if (m_lightningTimer > 0.0f && m_lightningLight) {
        m_lightningTimer -= dt;
        float t = std::max(0.0f, m_lightningTimer / kLightningFlashDuration);
        m_lightningLight->brightness = t * t * kLightningBrightness;
        m_lightningBoltAlpha = t;
        if (m_lightningTimer <= 0.0f) {
            m_lightningTimer = 0.0f;
            m_lightningLight->brightness = 0.0f;
            m_lightningBoltAlpha = 0.0f;
        }
    }
}

void Weather::updateAll(Instance* workspaceRoot, float dt, const Vector3& cameraPosition) {
    if (!workspaceRoot) return;
    for (auto const& [name, child] : workspaceRoot->getChildren()) {
        if (child->IsA("Weather")) {
            static_cast<Weather*>(child.get())->update(dt, cameraPosition);
        }
    }
}
