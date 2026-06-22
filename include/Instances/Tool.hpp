#pragma once
#include <Instances/Instance.hpp>
#include <Instances/BaseCube.hpp>
#include <Core/RCBNScriptSignal.hpp>

class Tool : public Instance {
    public:
        Tool(std::string name);

        virtual std::string getClassName() override {
            return "Tool";
        }

        virtual bool IsA(std::string className) override {
            return className == "Tool" || Instance::IsA(className);
        }

        enum class ToolHand { Right, Left, Both };

        bool Equipped = false;
        ToolHand Hand = ToolHand::Right;
        std::shared_ptr<RCBNScriptSignal> Activated;
        std::shared_ptr<BaseCube> Handle;

        virtual void setProperty(const std::string& name, const YAML::Node& value) override;

    private:
        // Tool固有のプロパティやメソッドをここに追加

};