#pragma once

#include <unordered_set>
#include <include/Math/Vector3.hpp>
#include <include/Math/Units.hpp>

#include <include/Instances/Instance.hpp>
#include <cstdint>
#include <memory>
#include <vector>

class Physics; // Forward declaration
class BaseCube;
class SurfaceMark;
class LightSource;
class ParticleEmitter;
class ScreenGuiObject;
class WorldGuiObject;
class SurfaceGui;
class PostEffect;
class Highlight;
class Lighting;
class CelestialBody;
class Weather;
class Terrain;
class Model;
class Humanoid;
class Skybox;
class Force;
class Attachment;
class BaseCubeBvh;

class Workspace : public Instance {
    private:
        // 信頼できるクラスのみに操作を許可
        friend class Script;
        friend class BaseCube;
        friend class Rope;
        friend class Rod;
        friend class Spring;
        friend class PrismaticConstraint;
        friend class Weld;
        friend class Motor;
        friend class BallSocket;
        friend class NoCollision;
        friend class Physics;
        friend class PhysicsConstraint;

        Physics* physicsEngine = nullptr; // Physics エンジンへのポインタ
        std::unique_ptr<Physics> m_ownedPhysics; // 所有するPhysicsインスタンス

        void registerScript(const std::shared_ptr<Instance>& s);
        void unregisterScript(const std::shared_ptr<Instance>& s);
        void registerCube(const std::shared_ptr<Instance>& c);
        void unregisterCube(const Instance* c);
        void registerConstraint(const std::shared_ptr<Instance>& c);
        void unregisterConstraint(const Instance* c);

        std::vector<Instance*> m_renderInstances;
        std::vector<BaseCube*> m_renderBaseCubes;
        std::vector<SurfaceMark*> m_renderSurfaceMarks;
        std::vector<LightSource*> m_renderLights;
        std::vector<ParticleEmitter*> m_renderParticleEmitters;
        std::vector<ScreenGuiObject*> m_renderScreenGuiObjects;
        std::vector<WorldGuiObject*> m_renderWorldGuiObjects;
        std::vector<SurfaceGui*> m_renderSurfaceGuis;
        std::vector<PostEffect*> m_renderPostEffects;
        std::vector<Highlight*> m_renderHighlights;
        std::vector<Lighting*> m_renderLightings;
        std::vector<CelestialBody*> m_renderCelestialBodies;
        std::vector<Weather*> m_renderWeathers;
        std::vector<Terrain*> m_renderTerrains;
        // 物理が毎フレーム全ツリーを走査しなくて済むよう、登録時に種別ごとに集めておく。
        std::vector<Instance*> m_renderConstraints; // PhysicsConstraint系
        std::vector<Model*> m_renderModels;
        std::vector<Humanoid*> m_renderHumanoids;
        std::vector<Skybox*> m_renderSkyboxes;
        std::vector<Force*> m_renderForces;
        std::vector<Attachment*> m_renderAttachments;
        // 選択レイキャスト用の空間インデックス(初回の問い合わせで生成する)
        std::unique_ptr<BaseCubeBvh> m_pickBvh;
        // 子孫の追加・削除・付け替えのたびに増える。変化検知用（値自体に意味は無い）。
        std::uint64_t m_treeRevision = 0;

    public:
        Vector3 Gravity = {0.0f, -METER_TO_STUD * EARTH_GRAVITY_MPS2, 0.0f};
        Vector3 Wind = {0.0f, 0.0f, 0.0f};  // Weatherが毎フレーム書き込む。ParticleEmitter::resolveWind()が読む
        bool PhysicsEnabled = true;

        std::vector<std::shared_ptr<Instance>> pendingInstances;
        // pendingInstancesの重複チェック用(線形探索だと1フレームに大量生成したときに二乗で遅い)。
        // 物理バックエンドがpendingInstancesを直接clearしても、registerCube/unregisterCubeが
        // 要素数の不一致を検出して作り直す。
        std::unordered_set<const Instance*> m_pendingInstanceSet;
        std::vector<std::shared_ptr<Instance>> pendingConstraints;
        std::vector<std::shared_ptr<Instance>> scripts;

        Workspace();
        virtual ~Workspace();

        virtual std::string getClassName() override;

        std::shared_ptr<Instance> clone() const override;

        bool IsA(std::string className) override;

        void setProperty(const std::string& name, const YAML::Node& value) override;

        // Physics エンジンをセット（外部から渡す場合）
        void setPhysicsEngine(Physics* engine) { physicsEngine = engine; }

        Physics* getPhysicsEngine() const { return physicsEngine; }

        // 自身が所有するPhysicsインスタンスを生成してセット
        void initPhysics();

        void setGravity(const Vector3& value);
        Vector3 getGravity() const { return Gravity; }
        bool getPhysicsEnabled() const { return PhysicsEnabled; }
        void setPhysicsEnabled(bool enabled) { PhysicsEnabled = enabled; }

        void registerRenderSubtree(Instance* root);
        void unregisterRenderSubtree(Instance* root);
        const std::vector<Instance*>& getRenderInstances() const { return m_renderInstances; }
        const std::vector<BaseCube*>& getRenderBaseCubes() const { return m_renderBaseCubes; }
        const std::vector<Instance*>& getRenderConstraints() const { return m_renderConstraints; }
        const std::vector<Model*>& getRenderModels() const { return m_renderModels; }
        const std::vector<Humanoid*>& getRenderHumanoids() const { return m_renderHumanoids; }
        const std::vector<Skybox*>& getRenderSkyboxes() const { return m_renderSkyboxes; }
        const std::vector<Force*>& getRenderForces() const { return m_renderForces; }
        const std::vector<Attachment*>& getRenderAttachments() const { return m_renderAttachments; }
        BaseCubeBvh& getPickBvh();
        std::uint64_t getTreeRevision() const { return m_treeRevision; }
        const std::vector<SurfaceMark*>& getRenderSurfaceMarks() const { return m_renderSurfaceMarks; }
        const std::vector<LightSource*>& getRenderLights() const { return m_renderLights; }
        const std::vector<ParticleEmitter*>& getRenderParticleEmitters() const { return m_renderParticleEmitters; }
        const std::vector<ScreenGuiObject*>& getRenderScreenGuiObjects() const { return m_renderScreenGuiObjects; }
        const std::vector<WorldGuiObject*>& getRenderWorldGuiObjects() const { return m_renderWorldGuiObjects; }
        const std::vector<SurfaceGui*>& getRenderSurfaceGuis() const { return m_renderSurfaceGuis; }
        const std::vector<PostEffect*>& getRenderPostEffects() const { return m_renderPostEffects; }
        const std::vector<Highlight*>& getRenderHighlights() const { return m_renderHighlights; }
        const std::vector<Lighting*>& getRenderLightings() const { return m_renderLightings; }
        const std::vector<CelestialBody*>& getRenderCelestialBodies() const { return m_renderCelestialBodies; }
        const std::vector<Weather*>& getRenderWeathers() const { return m_renderWeathers; }
        const std::vector<Terrain*>& getRenderTerrains() const { return m_renderTerrains; }
};
