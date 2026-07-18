#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Instances/Named.hpp>
#include <include/Math/Vector3.hpp>
#include <include/Math/CFrame.hpp>
#include <include/Util/Color4.hpp>
#include <vector>
#include <string>

class TerrainStreamer;
class Physics;
class BaseCube;

// 汎用パーティクル発生源。火・煙・水しぶき・汎用スクエアなど、プロパティの組み合わせだけで
// 表現する（タイプ別クラスは作らない）。BaseCube等の子として配置し、発生位置は毎フレーム
// 親のワールドCFrameから継承する（LightSourceと同じ設計。自身はPositionを持たない）。
// 描画はカメラ常時正面のビルボードで、テクスチャなしの単色頂点シェーダーを用いる。
class ParticleEmitter : public Named<ParticleEmitter, Instance> {
public:
    static constexpr const char* ClassName = "ParticleEmitter";

    Color4  StartColor       = Color4(1.0f, 1.0f, 1.0f, 1.0f);
    Color4  EndColor         = Color4(1.0f, 1.0f, 1.0f, 0.0f);
    float   StartSize        = 0.5f;
    float   EndSize          = 1.0f;
    float   EmitRate         = 20.0f;
    int     MaxParticles     = 200;
    float   Lifetime         = 1.5f;
    float   LifetimeVariance = 0.3f;
    float   Speed            = 5.0f;
    float   SpeedVariance    = 1.0f;
    Vector3 Direction        = Vector3(0.0f, 1.0f, 0.0f);
    float   SpreadAngle      = 15.0f;
    float   GravityScale     = 1.0f;
    float   SpinSpeed        = 0.0f;
    float   SpinSpeedVariance = 0.0f;
    float   Rotation         = 0.0f;
    float   RotationVariance = 0.0f;
    bool    Enabled          = true;
    float   WindScale        = 1.0f;   // Workspace::Windへの乗数（Gravityと同型）
    float   SpawnRadius      = 0.0f;   // >0で発生位置を水平円盤内にランダム分散（雨/雪の面積発生用）
    bool    CollisionCutoff  = false;  // 発生時と落下中の定期再計算で下方向レイキャストし、命中高度で消滅させる

    // 描画側が読む1粒子分のランタイム状態（PropertyRegistry非登録・非シリアライズ）
    struct Particle {
        Vector3 position;
        Vector3 velocity;
        float   age       = 0.0f;
        float   lifetime  = 1.0f;
        float   spinAngle = 0.0f;
        float   spinSpeed = 0.0f;
        float   killHeight = -1e8f;  // CollisionCutoff用。この高度以下になったら消滅（-1e8fは無効）
        float   cutoffRefresh = 0.0f;  // CollisionCutoff用。killHeight再計算までの残り秒数
    };

    ParticleEmitter();

    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;

    // 毎フレーム1回だけ呼ぶ（Rendererからは呼ばない。複数ビューポートで多重更新されるため）
    void update(float dt);
    // Enabledを無視して即座にcount個発生させる（Luauの:Emit(count)経由）
    void emit(int count);

    const std::vector<Particle>& getParticles() const { return particles; }

    // ワークスペース内の全ParticleEmitterを再帰的に見つけてupdate(dt)する
    static void updateAll(Instance* root, float dt);

private:
    std::vector<Particle> particles;
    float emitAccumulator = 0.0f;

    CFrame  resolveOriginCFrame() const;
    Vector3 resolveGravity();
    Vector3 resolveWind();

    // CollisionCutoff のレイキャスト先を1回のupdate/emit分キャッシュする（毎粒子のツリー走査を避ける）
    struct CutoffContext {
        TerrainStreamer* streamer = nullptr;  // 地形（ボクセルDDA、PhysX非依存）
        Physics*         physics  = nullptr;  // オブジェクト（Play中のみ存在）
        std::vector<BaseCube*> cubes;         // physics が無い時のフォールバック（エディタ用）
    };
    void    buildCutoffContext(CutoffContext& ctx);
    float   computeKillHeight(const Vector3& pos, const CutoffContext& ctx);
    void    spawnOne(const CFrame& originCFrame, const Vector3& gravity, const CutoffContext& ctx);
};
