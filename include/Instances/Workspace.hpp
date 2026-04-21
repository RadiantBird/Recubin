#pragma once

#include <include/Math/Vector3.hpp>

#include <include/Instances/Instance.hpp>

class Physics; // Forward declaration

class Workspace : public Instance {
    private:
        // 信頼できるクラスのみに操作を許可
        friend class Script;
        friend class BaseCube;

        Physics* physicsEngine = nullptr; // Physics エンジンへのポインタ

        void registerScript(Instance* s);
        void unregisterScript(Instance* s);
        void registerCube(Instance* c);

        // !! <DO NOT UNREGISTER THE CUBE, IT IS HANDLED BY "physics" CLASS.> !!
        // void unregisterCube(Instance* c) {
        //     pendingInstances.erase(std::remove(pendingInstances.begin(), pendingInstances.end(), c), pendingInstances.end());
        // }

    public:
        std::vector<Instance*> pendingInstances;
        std::vector<Instance*> scripts;
        
        Workspace();
        virtual ~Workspace();

        virtual std::string GetClassName() override;
        
        bool IsA(std::string className) override;

        // For debugging
        void buildTestSpace();

        // Physics エンジンをセット
        void setPhysicsEngine(Physics* engine) { physicsEngine = engine; }
};