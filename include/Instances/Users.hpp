#pragma once
#include <Instances/Instance.hpp>

// System/Users に配置される特殊コンテナ。System と同様にInsert Objectリストには
// 登録しない(エンジンが自動生成・管理する)。UseNetwork=falseの場合、中のUserの
// 名前は"User"のまま(spec.md参照)。複数Peer分のUser表現(接続ごとの生成/破棄)は
// v2.0応用tierとして今回のスコープ外。
class Users : public Instance {
public:
    Users() : Instance("Users") {}

    std::string getClassName() override { return "Users"; }

    bool IsA(std::string name) override {
        if (name == "Users") return true;
        return Instance::IsA(name);
    }
};
