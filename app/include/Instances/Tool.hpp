#pragma once
#include <Instances/Model.hpp>
#include <Instances/BaseCube.hpp>
#include <Core/RCBNScriptSignal.hpp>

class User;

class Tool : public Model {
    friend class User;

    public:
        Tool(std::string name);

        virtual std::string getClassName() override {
            return "Tool";
        }

        virtual bool IsA(std::string className) override {
            return className == "Tool" || Model::IsA(className);
        }

        enum class ToolHand { Right, Left, Both };

        bool Equipped = false;
        ToolHand Hand = ToolHand::Right;
        // ToolGrip のフレーム。armWorld * GripC0 == handleWorld * GripC1。
        CFrame GripC0 = CFrame(Vector3(0.0f, 0.0f, -1.0f));
        CFrame GripC1;
        std::shared_ptr<RCBNScriptSignal> Activated;
        std::shared_ptr<BaseCube> Handle;
        std::string m_handleName;  // Handle 参照名（制約の m_cube0Name と同じ規約で保存・解決）

        virtual void setProperty(const std::string& name, const YAML::Node& value) override;
        void onAncestorChanged() override;
        void collectInstanceReferences(std::vector<InstanceReference>& out) override {
            out.push_back({Handle, "BaseCube", "Tool.Handle",
                [this](std::shared_ptr<Instance> v) { setHandleReference(std::dynamic_pointer_cast<BaseCube>(v)); }});
        }

        std::shared_ptr<Instance> clone() const override;
        void setHandleReference(const std::shared_ptr<BaseCube>& handle);
        // Import-only compatibility hook for legacy Position/Rotation Tool data.
        // The caller supplies the old handle offset relative to GripC0.
        void setLegacyGripOffset(const CFrame& legacyOffset);
        const std::string& getHandlePath() const { return m_handleName; }
        void setHandlePath(const std::string& path);
        void remapClonedInstances(const CloneRemap& map) override;

    private:
        // Tool固有のプロパティやメソッドをここに追加
        void resolveHandle();  // m_handleName から Handle を遅延解決する
        std::weak_ptr<User> m_inventoryOwner;

};
