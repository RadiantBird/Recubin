#pragma once
#include <Instances/Instance.hpp>

// ==================================================================
//  StarterCharacter
//
//  キャラクターのテンプレートを保持するだけのコンテナ。
//  中にHumanoid・Root(Cube)・その他のCube/Sphereを通常のInsert Object操作で
//  組み立てる。Play開始時、この子要素が新規ModelにcloneされてWorkspaceに追加される。
// ==================================================================
class StarterCharacter : public Instance {
public:
    StarterCharacter() : Instance("StarterCharacter") {}

    std::string getClassName() override { return "StarterCharacter"; }

    bool IsA(std::string name) override {
        if (name == "StarterCharacter") return true;
        return Instance::IsA(name);
    }
};
