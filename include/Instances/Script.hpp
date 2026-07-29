#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Instances/Workspace.hpp>

// Forward declaration
struct lua_State;
class System;

class Script : public Instance {
    public:
        string Source = "";
        string Path = "";

        Workspace* lastWorkspace = nullptr;
        System*    lastSystem    = nullptr;  // Workspace外(System配下)で登録中の場合のみ非null

        bool Enabled        = true;
        bool isPrecompiled  = false; // true when Source holds raw .luauc bytecode
        bool Sleeping = false;
        bool Completed = false;  // スクリプト実行完了フラグ
        bool Aborted = false;    // エラーによる強制終了フラグ
        float SleepTime = 0.0f;
        float SleepRemaining = 0.0f;  // 残り待機時間

        // WaitChild による条件待機（Sleeping とは独立。子の出現をポーリングする）
        bool WaitingForChild = false;
        std::weak_ptr<Instance> WaitTarget;   // 子を探す対象
        std::string WaitChildName;            // 待っている子の名前
        float WaitTimeout = -1.0f;            // 秒。負なら無期限
        float WaitElapsed = 0.0f;             // 経過時間

        // PathfindingService::FindPath の非同期ナビメッシュ生成待機。
        // Sleeping/WaitingForChild と同様、通常のフレーム実行から除外するための状態。
        bool WaitingForPath = false;
        
        lua_State* Coroutine = nullptr;  // このスクリプト用のコルーチン
        int CoroutineRef = -1;  // Coroutineをluaのレジストリに保持する参照(LUA_NOREF(-1)相当)。
                                 // GCから保護するためのもので、Lのスタックには積みっぱなしにしない。

        void restart();  // 実行状態をリセットし、次のexecute()で最初から再実行させる

        virtual string getClassName() override;
        virtual bool IsA(std::string className) override;
        virtual void setProperty(const std::string& name, const YAML::Node& value) override;
        void onAncestorChanged() override;
        std::shared_ptr<Instance> clone() const override;

        Script(string path = "");
};
