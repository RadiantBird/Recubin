#pragma once
#include <Instances/Instance.hpp>

class Tool : public Instance {
    public:
        Tool(std::string name) : Instance(name) {}

        virtual std::string GetClassName() override {
            return "Tool";
        }

        virtual bool IsA(std::string className) override {
            return className == "Tool" || Instance::IsA(className);
        }

        bool Equipped = false;
        
    private:
        // Tool固有のプロパティやメソッドをここに追加

};