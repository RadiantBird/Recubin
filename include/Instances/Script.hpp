#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Instances/Workspace.hpp>

// Forward declaration
struct lua_State;

class Script : public Instance {
    public:
        string Source = R"(print("Hello world!"))";
        Workspace* lastWorkspace = nullptr;

        bool Enabled = true;
        bool Sleeping = false;
        bool Completed = false;  // スクリプト実行完了フラグ
        float SleepTime = 0.0f;
        float SleepRemaining = 0.0f;  // 残り待機時間
        
        lua_State* Coroutine = nullptr;  // このスクリプト用のコルーチン
        
        virtual string GetClassName() override;
        bool IsA(std::string className) override;
        void onAncestorChanged() override;

        Script();
};
