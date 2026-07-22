#pragma once
#include <Instances/Instance.hpp>

// System/Users に配置される特殊コンテナ。System と同様にInsert Objectリストには
// 登録しない(エンジンが自動生成・管理する)。Offlineでは中のUser名は"User"のまま。
// ネットワークHost/ClientではHostが割り当てたPeerIdによりUser_<id>を生成・破棄する。
class Users : public Instance {
public:
    Users() : Instance("Users") {}

    std::string getClassName() override { return "Users"; }

    bool IsA(std::string name) override {
        if (name == "Users") return true;
        return Instance::IsA(name);
    }
};
