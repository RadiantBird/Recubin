#pragma once

#include <include/Math/Vector3.hpp>

#include <include/Instances/Instance.hpp>

class Workspace : public Instance {
    public:
        Vector3 Gravity = Vector3(0, -9.8f, 0);

        std::vector<Instance*> pendingInstances;

        void addChild(Instance* child) override {
            if (!child) return;
            
            // 通常のツリー追加
            Instance::addChild(child);

            // 物理エンジンへの通知用リストに追加
            pendingInstances.push_back(child);
        }

        Workspace () : Instance("Workspace") {};
};