#pragma once

#include <include/Math/Vector3.hpp>

#include <include/Instances/Instance.hpp>

class Workspace : public Instance {
    private:
        // 信頼できるクラスのみに操作を許可
        friend class Script;
        friend class BaseCube;

        void registerScript(Instance* s) {
            scripts.push_back(s);
        }

        void unregisterScript(Instance* s) {
            scripts.erase(std::remove(scripts.begin(), scripts.end(), s), scripts.end());
        }

        void registerCube(Instance* c) {
            pendingInstances.push_back(c);
        }

        // !! <DO NOT UNREGISTER THE CUBE, IT IS HANDLED BY "physics" CLASS.> !!
        // void unregisterCube(Instance* c) {
        //     pendingInstances.erase(std::remove(pendingInstances.begin(), pendingInstances.end(), c), pendingInstances.end());
        // }

    public:
        std::vector<Instance*> pendingInstances;
        std::vector<Instance*> scripts;
        
        Workspace() : Instance("Workspace") {};

        virtual string GetClassName() override {
            return "Workspace";
        }
        
        bool IsA(std::string className) override {
            if (className == "Workspace") {
                return true;
            } else {
                return Instance::IsA(className);
            }
        }

        // For debugging
        void buildTestSpace() {
            // TODO
        }
};