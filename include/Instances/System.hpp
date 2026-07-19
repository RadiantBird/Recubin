#pragma once
#include <Instances/Instance.hpp>
#include <Core/RCBNScriptSignal.hpp>
#include <Math/Vector2.hpp>
#include <memory>

class System : public Instance {
    private:
        // Workspace::registerScriptと同じ方式(信頼できるクラスのみに操作を許可)
        friend class Script;

        void registerScript(const std::shared_ptr<Instance>& s);
        void unregisterScript(const std::shared_ptr<Instance>& s);

    public:
        std::shared_ptr<RCBNScriptSignal> Heartbeat;
        // ネットワークロール変更時(ホスト移行等)に (oldRole, newRole) の文字列2引数で発火する
        std::shared_ptr<RCBNScriptSignal> NetworkRoleChanged;

        // Workspace外(System配下)に置かれたスクリプトの実行リスト。
        // Workspace::scriptsと相互排他(Workspace配下に入ったらそちらへ移る)。
        std::vector<std::shared_ptr<Instance>> scripts;

        // 安全対策の許容値。意図的にLuauへバインドしない
        // (DispatchTable/SetterTableに登録しないこと)
        int   MaxClonesPerFrame        = 1000;
        int   MaxRestartsPerFrame      = 100;
        int   MaxTasksPerFrame         = 1000; // task.spawn/delayの1フレームあたりの生成上限
        float ScriptLoopTimeoutSeconds = 2.0f; // 0以下でループタイムアウト無効

        // ScreenGui(Norm::Pixel)の自動スケーリング基準解像度。Luauへは読み取り専用で公開。
        Vector2 BaseResolution = {1920.f, 1080.f};

        // trueの場合、LocalScript/Scriptが区別されて動作する(Client側ではLocalScriptのみ実行)。
        // falseの場合、区別されず、ネットワーク通信も一切行わない。
        bool UseNetwork = false;

        System(string name = "System");
        string getClassName() override;
        bool IsA(std::string className) override;
        void addChild(std::shared_ptr<Instance> child) override;
        void setProperty(const std::string& name, const YAML::Node& value) override;
};
