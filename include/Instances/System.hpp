#pragma once
#include <Instances/Instance.hpp>
#include <Core/RCBNScriptSignal.hpp>
#include <Math/Vector2.hpp>
#include <memory>

class System : public Instance {
    public:
        std::shared_ptr<RCBNScriptSignal> Heartbeat;

        // 安全対策の許容値。意図的にLuauへバインドしない
        // (DispatchTable/SetterTableに登録しないこと)
        int   MaxClonesPerFrame        = 1000;
        int   MaxRestartsPerFrame      = 100;
        float ScriptLoopTimeoutSeconds = 2.0f; // 0以下でループタイムアウト無効

        // ScreenGui(Norm::Pixel)の自動スケーリング基準解像度。Luauへは読み取り専用で公開。
        Vector2 BaseResolution = {1920.f, 1080.f};

        System(string name = "System");
        string getClassName() override;
        bool IsA(std::string className) override;
        void addChild(std::shared_ptr<Instance> child) override;
        void setProperty(const std::string& name, const YAML::Node& value) override;
};
