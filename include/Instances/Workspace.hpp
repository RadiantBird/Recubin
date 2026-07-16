#pragma once

#include <include/Math/Vector3.hpp>
#include <include/Math/Units.hpp>

#include <include/Instances/Instance.hpp>
#include <memory>

class Physics; // Forward declaration

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

        Physics* physicsEngine = nullptr; // Physics エンジンへのポインタ
        std::unique_ptr<Physics> m_ownedPhysics; // 所有するPhysicsインスタンス

        void registerScript(const std::shared_ptr<Instance>& s);
        void unregisterScript(const std::shared_ptr<Instance>& s);
        void registerCube(const std::shared_ptr<Instance>& c);
        void registerConstraint(const std::shared_ptr<Instance>& c);

        // !! <DO NOT UNREGISTER THE CUBE, IT IS HANDLED BY "physics" CLASS.> !!
        // void unregisterCube(Instance* c) {
        //     pendingInstances.erase(std::remove(pendingInstances.begin(), pendingInstances.end(), c), pendingInstances.end());
        // }

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

        bool IsA(std::string className) override;

        void setProperty(const std::string& name, const YAML::Node& value) override;

        // Physics エンジンをセット（外部から渡す場合）
        void setPhysicsEngine(Physics* engine) { physicsEngine = engine; }

        Physics* getPhysicsEngine() const { return physicsEngine; }

        // 自身が所有するPhysicsインスタンスを生成してセット
        void initPhysics();
};