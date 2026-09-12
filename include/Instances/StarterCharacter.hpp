#pragma once
#include <Math/Vector3.hpp>
#include <Instances/Spatial.hpp>
#include <Instances/Model.hpp>

// ==================================================================
//  StarterCharacter
//
//  キャラクターのテンプレートを保持するだけのコンテナ。
//  中にHumanoid・Root(Cube)・その他のCube/Sphereを通常のInsert Object操作で
//  組み立てる。Play開始時、この子要素が新規ModelにcloneされてWorkspaceに追加される。
// ==================================================================
class StarterCharacter : public Model {
public:
    StarterCharacter()
        : Model(Vector3{0, 0, 0}, Vector3{1, 1, 1}) {
    }

    std::string getClassName() override { return "StarterCharacter"; }

    bool IsA(std::string name) override {
        if (name == "StarterCharacter") return true;
        return Model::IsA(name);
    }
};
