#pragma once
#include <Instances/Instance.hpp>
#include <Instances/BaseCube.hpp>
#include <Core/RCBNScriptSignal.hpp>

class User;

class Tool : public Instance {
    friend class User;

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
        // Handle を手へ装着するときに適用する、手基準のローカルオフセット。
        // Position / Rotation としてエディターおよび Luau に公開する。
        Vector3 Position;
        Quaternion Rotation;
        std::shared_ptr<RCBNScriptSignal> Activated;
        std::shared_ptr<BaseCube> Handle;
        std::string m_handleName;  // Handle 参照名（制約の m_cube0Name と同じ規約で保存・解決）

        virtual void setProperty(const std::string& name, const YAML::Node& value) override;
        void onAncestorChanged() override;

    private:
        // Tool固有のプロパティやメソッドをここに追加
        void resolveHandle();  // m_handleName から Handle を遅延解決する
        std::weak_ptr<User> m_inventoryOwner;

};
