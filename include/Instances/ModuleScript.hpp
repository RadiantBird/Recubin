#pragma once
#include <include/Instances/Script.hpp>

// require(moduleScript)で読み込まれる再利用モジュール。
// Script派生だがWorkspace/Systemの実行リストには登録せず(onAncestorChangedを
// バイパス)、自動実行されない。実行はrequire時に1回だけ行われ、返り値が
// キャッシュされる(LuauEngine::luafn_require参照)。
class ModuleScript : public Script {
    public:
        ModuleScript(string path = "");

        string getClassName() override;
        bool IsA(std::string className) override;
        void onAncestorChanged() override;
        std::shared_ptr<Instance> clone() const override;
};
