#pragma once
#include <include/Instances/Script.hpp>

// UseNetwork=trueのとき、Clientではこちらのみが実行される(Scriptは実行されない)。
// UseNetwork=falseのときはScriptと全く同じ扱い(spec.md「UseNetworkがfalseならScriptと同じ」)。
// 実行可否のフィルタ自体はLuauEngine::executeWorkspaceScriptsが持つ(このクラスは目印のみ)。
class LocalScript : public Script {
    public:
        LocalScript(string path = "");

        string getClassName() override;
        bool IsA(std::string className) override;
        std::shared_ptr<Instance> clone() const override;
};
