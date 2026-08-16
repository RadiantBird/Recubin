#pragma once
#include <Instances/Instance.hpp>
#include <Math/Vector3.hpp>
#include <memory>
#include <string>
#include <vector>
#include <Math/CFrame.hpp>

struct R6JointBinding {
    std::string jointName;
    std::string partName;
    CFrame rootToJoint;
    CFrame jointToPartBind;
};

// デフォルトキャラクターリグ(Humanoid + Root/Head/Torso/LeftArm/RightArm/LeftLeg/RightLegの7パーツ)を
// 生成し、parentの子としてaddChildする。
// User::spawnCharacterのフォールバック生成(StarterCharacter用)と、
// エディターツールバーの「リグビルダー」ボタン(任意の親、通常は新規Model)の両方から共有利用する。
namespace CharacterRig {
    const std::vector<R6JointBinding>& r6JointBindings();
    const R6JointBinding* findR6Joint(const std::string& jointName);
    CFrame applyR6Joint(const CFrame& root, const R6JointBinding& binding, const CFrame& delta);
    // parentの子としてリグ(Humanoid+7パーツ)を追加する。basePosはRootのワールド座標
    // (省略時は原点)。追加後、Torso/Head/腕/脚はRootからの相対位置へ自動配置される。
    void buildDefaultRigParts(const std::shared_ptr<Instance>& parent, const Vector3& basePos = Vector3(0.0f, 0.0f, 0.0f));
}
