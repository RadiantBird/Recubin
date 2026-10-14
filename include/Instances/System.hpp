#pragma once
#include <Instances/Instance.hpp>
#include <Core/RCBNScriptSignal.hpp>
#include <Math/Vector2.hpp>
#include <memory>
#include <string>

class System : public Instance {
    private:
        // Workspace::registerScriptと同じ方式(信頼できるクラスのみに操作を許可)
        friend class Script;

        void registerScript(const std::shared_ptr<Instance>& s);
        void unregisterScript(const std::shared_ptr<Instance>& s);

    public:
        enum class CameraMode { Character = 0, Free, Program };

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
        // エディター用: trueならビューポートをBaseResolutionの縦横比で黒帯付き表示する。
        // falseなら黒帯なしでパネル全体に描画し、Pixel単位の画面GUIは縦横同率で拡大縮小する。
        // ランタイム(game_main)の描画には影響しない。Luauからは不可視。
        bool UseFixedResolution = true;
        // ランタイム用: trueならウィンドウの縦横比に関わらず BaseResolution の縦横比を保ち、
        // 余白を黒帯にする（RecubinEngine）。falseならウィンドウ全体に描画する。Luauからは読み取り専用。
        bool LockAspectRatio = false;
        // ランタイム用: L キーでの Free/Character 切り替えを許可するか（RecubinEngine）。Luauからは読み取り専用。
        bool AllowControlModeSwitch = false;
        // ランタイム用: User:RequestWorkspaceSwitch() による Workspace 切り替えを許可するか（RecubinEngine）。
        bool AllowWorkspaceSwitch = false;

        // trueの場合、LocalScript/Scriptが区別されて動作する(Client側ではLocalScriptのみ実行)。
        // falseの場合、区別されず、ネットワーク通信も一切行わない。
        bool UseNetwork = false;
        // Clientで他プレイヤー・同期物体を何秒前の状態として補間表示するか。
        // 長いほど通信の揺れに強く滑らか、短いほど現在に近い(既定0.1秒)
        float NetworkInterpolationDelay = 0.1f;

        // Opt-in system extensions. These are
        // intentionally read-only from Luau; only the scene/editor may configure them.
        // Public PascalCase is retained for the existing reflection/YAML API.
        bool EnableIOAPI = false;
        bool EnableIPCAPI = false;
        bool EnableExternalFileAccess = false;

        // パッケージ済みゲーム起動時にUserへ適用するカメラモード。
        CameraMode DefaultCameraMode = CameraMode::Character;

        // falseの場合、ゲーム開始時にローカルPlayerCharacterを自動生成しない。
        bool AutoSpawnPlayerCharacter = true;

        System(string name = "System");
        string getClassName() override;
        bool IsA(std::string className) override;
        void addChild(std::shared_ptr<Instance> child) override;
        void setProperty(const std::string& name, const YAML::Node& value) override;
};
