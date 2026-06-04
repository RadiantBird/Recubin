#pragma once
#include <Instances/Instance.hpp>
#include <Instances/BaseCube.hpp>
#include <Core/RCBNScriptSignal.hpp>

class Tool : public Instance {
    public:
        Tool(std::string name);

        virtual std::string GetClassName() override {
            return "Tool";
        }

        virtual bool IsA(std::string className) override {
            return className == "Tool" || Instance::IsA(className);
        }

        bool Equipped = false;
        std::shared_ptr<RCBNScriptSignal> Activated;
        std::shared_ptr<BaseCube> Handle;

    private:
        // Tool固有のプロパティやメソッドをここに追加

};