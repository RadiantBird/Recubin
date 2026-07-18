#include <include/Instances/ParticleEmitter.hpp>
#include <include/Instances/Spatial.hpp>
#include <include/Instances/Workspace.hpp>
#include <include/Core/Physics.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <include/Core/Terrain.hpp>
#include <include/Core/TerrainStreamer.hpp>
#include <include/Instances/BaseCube.hpp>
#include <include/Core/SystemState.hpp>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <utility>

static const bool s_particleEmitterRegistered = []{
    using namespace PropertyRegistry;
    registerClass("ParticleEmitter", {
        field<&ParticleEmitter::StartColor>       ("StartColor"),
        field<&ParticleEmitter::EndColor>         ("EndColor"),
        field<&ParticleEmitter::StartSize>        ("StartSize",         0.0f, 50.0f,  0.1f).clampLua(),
        field<&ParticleEmitter::EndSize>          ("EndSize",           0.0f, 50.0f,  0.1f).clampLua(),
        field<&ParticleEmitter::EmitRate>         ("EmitRate",          0.0f, 500.0f, 1.0f).clampLua(),
        field<&ParticleEmitter::MaxParticles>     ("MaxParticles",      1.0f, 5000.0f,1.0f).clampLua(),
        field<&ParticleEmitter::Lifetime>         ("Lifetime",          0.01f,60.0f,  0.05f).clampLua(),
        field<&ParticleEmitter::LifetimeVariance> ("LifetimeVariance",  0.0f, 30.0f,  0.05f).clampLua(),
        field<&ParticleEmitter::Speed>            ("Speed",             0.0f, 500.0f, 0.5f).clampLua(),
        field<&ParticleEmitter::SpeedVariance>    ("SpeedVariance",     0.0f, 500.0f, 0.5f).clampLua(),
        field<&ParticleEmitter::Direction>        ("Direction"),
        field<&ParticleEmitter::SpreadAngle>      ("SpreadAngle",       0.0f, 180.0f, 1.0f).clampLua(),
        field<&ParticleEmitter::GravityScale>     ("GravityScale",     -5.0f, 5.0f,   0.05f).clampLua(),
        field<&ParticleEmitter::SpinSpeed>        ("SpinSpeed",       -720.0f, 720.0f,1.0f).clampLua(),
        field<&ParticleEmitter::SpinSpeedVariance>("SpinSpeedVariance", 0.0f, 720.0f, 1.0f).clampLua(),
        field<&ParticleEmitter::Rotation>         ("Rotation",          0.0f, 360.0f, 1.0f).clampLua(),
        field<&ParticleEmitter::RotationVariance> ("RotationVariance",  0.0f, 180.0f, 1.0f).clampLua(),
        field<&ParticleEmitter::Enabled>          ("Enabled"),
        field<&ParticleEmitter::WindScale>        ("WindScale",        -5.0f, 5.0f,   0.05f).clampLua(),
        field<&ParticleEmitter::SpawnRadius>      ("SpawnRadius",       0.0f, 500.0f, 0.5f).clampLua(),
        field<&ParticleEmitter::CollisionCutoff>  ("CollisionCutoff"),
    });
    return true;
}();

ParticleEmitter::ParticleEmitter() : Named<ParticleEmitter, Instance>("ParticleEmitter") {}

bool ParticleEmitter::IsA(std::string className) {
    if (className == "ParticleEmitter") return true;
    return Instance::IsA(className);
}

void ParticleEmitter::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "ParticleEmitter", name, value)) return;
    Instance::setProperty(name, value);
}

std::shared_ptr<Instance> ParticleEmitter::clone() const {
    auto copy = std::make_shared<ParticleEmitter>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "ParticleEmitter");
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}

CFrame ParticleEmitter::resolveOriginCFrame() const {
    auto par = Parent.lock();
    if (par && par->IsA("Spatial")) {
        return static_cast<Spatial*>(par.get())->getWorldCFrame();
    }
    return CFrame();
}

Vector3 ParticleEmitter::resolveGravity() {
    if (Instance* wsInst = findFirstAncestorWorkspace()) {
        return static_cast<Workspace*>(wsInst)->Gravity;
    }
    return Vector3(0.0f, -METER_TO_STUD * EARTH_GRAVITY_MPS2, 0.0f);
}

Vector3 ParticleEmitter::resolveWind() {
    if (Instance* wsInst = findFirstAncestorWorkspace()) {
        return static_cast<Workspace*>(wsInst)->Wind;
    }
    return Vector3(0.0f, 0.0f, 0.0f);
}

// OBB スラブ法レイキャスト。レイをローカル空間に変換してAABBテストする（ViewportPanelの選択判定と同ロジック）。
// ヒットしなければ負値を返す。
static float obbRayHit(const Vector3& ori, const Vector3& dir, const CFrame& worldCF, const Vector3& size) {
    Quaternion invRot = worldCF.Rotation.conjugate();
    Vector3 lo = invRot.rotate(ori - worldCF.Position);
    Vector3 ld = invRot.rotate(dir);
    float ld3[3] = { ld.x, ld.y, ld.z };
    float lo3[3] = { lo.x, lo.y, lo.z };
    float hs[3]  = { size.x * 0.5f, size.y * 0.5f, size.z * 0.5f };
    float tmin = -1e30f, tmax = 1e30f;
    for (int i = 0; i < 3; ++i) {
        if (std::abs(ld3[i]) < 1e-8f) {
            if (lo3[i] < -hs[i] || lo3[i] > hs[i]) return -1.0f;
        } else {
            float t1 = (-hs[i] - lo3[i]) / ld3[i];
            float t2 = ( hs[i] - lo3[i]) / ld3[i];
            if (t1 > t2) std::swap(t1, t2);
            tmin = (std::max)(tmin, t1);
            tmax = (std::min)(tmax, t2);
            if (tmax < tmin) return -1.0f;
        }
    }
    if (tmax < 0.0f) return -1.0f;
    return (tmin >= 0.0f) ? tmin : tmax;
}

void ParticleEmitter::buildCutoffContext(CutoffContext& ctx) {
    Instance* wsInst = findFirstAncestorWorkspace();
    if (!wsInst) return;
    Workspace* workspace = static_cast<Workspace*>(wsInst);

    // 地形はPhysX非依存のボクセルDDAで判定する（エディタ中や物理未登録チャンクでも貫通しない）
    for (auto const& [name, child] : workspace->getChildren()) {
        if (!child->IsA("Terrain")) continue;
        Terrain* terrain = static_cast<Terrain*>(child.get());
        if (terrain->Enabled && terrain->streamer) ctx.streamer = terrain->streamer.get();
        break;
    }

    // Physicsオブジェクトとシーンはエディタ中も存在するが、キューブアクターはPlay中しか
    // 揃わない（編集中はプロパティ変更で部分的に生成されるだけ）ため、Play中のみPhysXを採用する
    Physics* physics = workspace->getPhysicsEngine();
    if (physics && physics->getScene() && SystemState::get().isPlaying) ctx.physics = physics;

    // PhysXシーンが無い間（エディタ編集中）はBaseCubeへのOBBレイキャストで代用する
    if (!ctx.physics) {
        auto collect = [&](auto& self, Instance* inst) -> void {
            if (inst->IsA("BaseCube")) {
                BaseCube* bc = static_cast<BaseCube*>(inst);
                if (bc->CanCollide) ctx.cubes.push_back(bc);
            }
            for (auto const& [name, child] : inst->getChildren())
                self(self, child.get());
        };
        collect(collect, wsInst);
    }
}

float ParticleEmitter::computeKillHeight(const Vector3& pos, const CutoffContext& ctx) {
    const Vector3 down(0.0f, -1.0f, 0.0f);
    float best = -1e8f;

    if (ctx.streamer) {
        Vector3 hitPos;
        if (ctx.streamer->raycastVoxel(pos, down, 2000.0f, hitPos))
            best = hitPos.y + 0.05f;
    }

    if (ctx.physics) {
        RaycastHit hit;
        if (ctx.physics->raycast(pos, down, 2000.0f, hit, nullptr) && hit.hit)
            best = std::max(best, hit.position.y + 0.05f);
    } else {
        for (BaseCube* bc : ctx.cubes) {
            float t = obbRayHit(pos, down, bc->getWorldCFrame(), bc->Size);
            if (t >= 0.0f && t <= 2000.0f)
                best = std::max(best, pos.y - t + 0.05f);
        }
    }

    return best;
}

void ParticleEmitter::spawnOne(const CFrame& originCFrame, const Vector3& gravity, const CutoffContext& ctx) {
    (void)gravity; // 初速のみここで決める。重力はupdate()側の積分で毎フレーム適用する
    auto frand01 = []() { return static_cast<float>(std::rand()) / RAND_MAX; };
    auto frandPM = [&]() { return frand01() * 2.0f - 1.0f; };

    Vector3 localDir = Direction.length() > 0.0001f ? Direction.normalize() : Vector3(0.0f, 1.0f, 0.0f);
    Vector3 worldDir = originCFrame.Rotation.rotate(localDir);

    Vector3 arbitrary = (std::fabs(worldDir.y) < 0.99f) ? Vector3(0.0f, 1.0f, 0.0f) : Vector3(1.0f, 0.0f, 0.0f);
    Vector3 tangent   = Vector3::Cross(arbitrary, worldDir).normalize();
    Vector3 bitangent = Vector3::Cross(worldDir, tangent);

    float spreadRad = SpreadAngle * pi / 180.0f;
    float theta = frand01() * spreadRad;
    float phi   = frand01() * 2.0f * pi;
    Vector3 dir = worldDir * std::cos(theta) +
                  (tangent * std::cos(phi) + bitangent * std::sin(phi)) * std::sin(theta);
    if (dir.length() < 0.0001f) dir = worldDir;

    Particle p;
    p.position  = originCFrame.Position;
    p.velocity  = dir.normalize() * (Speed + frandPM() * SpeedVariance);
    p.lifetime  = std::max(0.01f, Lifetime + frandPM() * LifetimeVariance);
    p.spinAngle = (Rotation  + frandPM() * RotationVariance)  * pi / 180.0f;
    p.spinSpeed = (SpinSpeed + frandPM() * SpinSpeedVariance) * pi / 180.0f;

    if (SpawnRadius > 0.0001f) {
        float theta = frand01() * 2.0f * pi;
        float r     = std::sqrt(frand01()) * SpawnRadius; // sqrtで面積一様分布に
        p.position  = p.position + Vector3(std::cos(theta) * r, 0.0f, std::sin(theta) * r);
    }

    p.killHeight = -1e8f;
    if (CollisionCutoff) {
        p.killHeight    = computeKillHeight(p.position, ctx);
        p.cutoffRefresh = frand01() * 0.2f; // 全粒子が同一フレームに再計算してスパイクしないようずらす
    }

    particles.push_back(p);
}

void ParticleEmitter::update(float dt) {
    Vector3 gravity = resolveGravity();
    Vector3 wind    = resolveWind();

    CutoffContext cutoffCtx;
    if (CollisionCutoff && (Enabled || !particles.empty()))
        buildCutoffContext(cutoffCtx);

    for (int i = static_cast<int>(particles.size()) - 1; i >= 0; --i) {
        Particle& p = particles[static_cast<size_t>(i)];
        p.age += dt;
        if (p.age >= p.lifetime || (p.killHeight > -1e7f && p.position.y <= p.killHeight)) {
            particles[static_cast<size_t>(i)] = particles.back();
            particles.pop_back();
            continue;
        }
        p.velocity  = p.velocity + gravity * (GravityScale * dt) + wind * (WindScale * dt);
        p.position  = p.position + p.velocity * dt;
        p.spinAngle += p.spinSpeed * dt;

        // 風で流されてスポーン列の真下とズレるため、落下中も定期的にkillHeightを再計算する
        if (CollisionCutoff) {
            p.cutoffRefresh -= dt;
            if (p.cutoffRefresh <= 0.0f) {
                p.killHeight    = computeKillHeight(p.position, cutoffCtx);
                // 空振り時（谷・ボイド上空）は長いDDAを毎回繰り返さないよう間隔を延ばす
                p.cutoffRefresh = (p.killHeight > -1e7f) ? 0.2f : 0.5f;
            }
        }
    }

    if (Enabled) {
        CFrame originCFrame = resolveOriginCFrame();
        emitAccumulator += EmitRate * dt;
        int toSpawn = static_cast<int>(std::floor(emitAccumulator));
        emitAccumulator -= static_cast<float>(toSpawn);
        for (int i = 0; i < toSpawn && static_cast<int>(particles.size()) < MaxParticles; ++i)
            spawnOne(originCFrame, gravity, cutoffCtx);
    }
}

void ParticleEmitter::emit(int count) {
    CFrame originCFrame = resolveOriginCFrame();
    Vector3 gravity = resolveGravity();
    CutoffContext ctx;
    if (CollisionCutoff) buildCutoffContext(ctx);
    for (int i = 0; i < count && static_cast<int>(particles.size()) < MaxParticles; ++i)
        spawnOne(originCFrame, gravity, ctx);
}

void ParticleEmitter::updateAll(Instance* root, float dt) {
    if (!root) return;
    if (root->IsA("ParticleEmitter")) static_cast<ParticleEmitter*>(root)->update(dt);
    for (auto const& [name, child] : root->getChildren())
        updateAll(child.get(), dt);
}
