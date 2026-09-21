#pragma once

#include <include/Math/Vector3.hpp>
#include <include/Math/Units.hpp>

#include <include/Instances/Instance.hpp>
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
class Weather;
class Terrain;

class Workspace : public Instance {
    private:
        // 信頼できるクラスのみに操作を許可
        friend class Script;
        friend class BaseCube;
        friend class Rope;
        friend class Rod;
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
        std::vector<Weather*> m_renderWeathers;
        std::vector<Terrain*> m_renderTerrains;

    public:
        Vector3 Gravity = {0.0f, -METER_TO_STUD * EARTH_GRAVITY_MPS2, 0.0f};
        Vector3 Wind = {0.0f, 0.0f, 0.0f};  // Weatherが毎フレーム書き込む。ParticleEmitter::resolveWind()が読む
        bool PhysicsEnabled = true;

        std::vector<std::shared_ptr<Instance>> pendingInstances;
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
        const std::vector<SurfaceMark*>& getRenderSurfaceMarks() const { return m_renderSurfaceMarks; }
        const std::vector<LightSource*>& getRenderLights() const { return m_renderLights; }
        const std::vector<ParticleEmitter*>& getRenderParticleEmitters() const { return m_renderParticleEmitters; }
        const std::vector<ScreenGuiObject*>& getRenderScreenGuiObjects() const { return m_renderScreenGuiObjects; }
        const std::vector<WorldGuiObject*>& getRenderWorldGuiObjects() const { return m_renderWorldGuiObjects; }
        const std::vector<SurfaceGui*>& getRenderSurfaceGuis() const { return m_renderSurfaceGuis; }
        const std::vector<PostEffect*>& getRenderPostEffects() const { return m_renderPostEffects; }
        const std::vector<Highlight*>& getRenderHighlights() const { return m_renderHighlights; }
        const std::vector<Lighting*>& getRenderLightings() const { return m_renderLightings; }
        const std::vector<Weather*>& getRenderWeathers() const { return m_renderWeathers; }
        const std::vector<Terrain*>& getRenderTerrains() const { return m_renderTerrains; }
};
