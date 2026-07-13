#pragma once
#include <Instances/Instance.hpp>
#include <memory>

// デフォルトキャラクターリグ(Humanoid + Root/Head/Torso/LeftArm/RightArm/LeftLeg/RightLegの7パーツ)を
// 生成し、parentの子としてaddChildする。
// User::spawnCharacterのフォールバック生成(StarterCharacter用)と、
// エディターツールバーの「リグビルダー」ボタン(任意の親、通常は新規Model)の両方から共有利用する。
namespace CharacterRig {
    void buildDefaultRigParts(const std::shared_ptr<Instance>& parent);
}
