#pragma once

#include <Instances/Instance.hpp>
#include <Instances/Cube.hpp>
#include <Instances/Sphere.hpp>
#include <Util/Color4.hpp>
#include <Core/RCBNScriptSignal.hpp>

class Physics;   // Forward declaration
class Animation; // Forward declaration

// ==================================================================
//  Humanoid
//
//  StarterCharacter内のテンプレート、またはそのclone後にRoot/Torso/Head/
//  LeftArm/RightArm/LeftLeg/RightLegという名前の兄弟Cube/Sphereを探して
//  保持し、キャラクターの移動・ジャンプ・接地判定・歩行アニメーション・
//  一人称時の身体非表示を一括して行う。
// ==================================================================
class Humanoid : public Instance {
public:
    float WalkSpeed = 5.0f;
    float JumpPower = 7.0f;

    // JumpPowerから逆算した跳躍到達高さ(stud)。設定するとJumpPowerが自動計算される
    float getJumpHeight() const;
    void  setJumpHeight(float height);

    // --- ヘルス ---
    float Health      = 100.0f;
    float MaxHealth   = 100.0f;
    float RespawnTime = 5.0f;   // 死亡後この秒数で再生成される
    std::shared_ptr<RCBNScriptSignal> Died; // Health<=0 で1度だけ発火

    // 兄弟パーツへの参照（resolveParts()で解決する）
    std::shared_ptr<BaseCube>   Root;
    std::shared_ptr<BaseCube>   Torso;
    std::shared_ptr<BaseCube> Head;
    std::shared_ptr<BaseCube>   LeftArm;
    std::shared_ptr<BaseCube>   RightArm;
    std::shared_ptr<BaseCube>   LeftLeg;
    std::shared_ptr<BaseCube>   RightLeg;

    Humanoid();

    std::string getClassName() override { return "Humanoid"; }
    bool IsA(std::string className) override;
    void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;

    // characterModel(クローン後のModel)の子から名前("Root","Torso",...)でパーツを解決する
    void resolveParts(Instance* characterModel);

    // WASD相当の入力を受けて移動・回転・歩行アニメ・接地判定・身体パーツの再配置を行う
    // flatForward/flatRight: カメラ由来の水平方向ベクトル, targetMoveDir: 押下キーから求めた移動方向(未押下なら長さ0)
    // leftArmRaised/rightArmRaised: User側のTool装備状態に応じた腕ポーズの上書き指示(Humanoid自体はToolを知らない)
    void move(const Vector3& flatForward, const Vector3& flatRight, bool isPressingMove,
              const Vector3& targetMoveDir, bool ctrlLockEnabled, Physics* physics,
              bool leftArmRaised = false, bool rightArmRaised = false);

    // 接地中のみJumpPowerで上方向の速度をセットする
    void jump();

    // パス追従用: targetへ向けて1フレーム分移動する（move()のXZ方向移動ロジックを流用）。
    // arrivalRadius以内に到達していれば何もせず true を返す。
    bool moveToward(const Vector3& target, Physics* physics, float arrivalRadius = 1.0f);

    // --- ヘルス / 死亡 ---
    void setHealth(float v);          // クランプして設定。0以下への遷移で Died を発火
    void takeDamage(float n);         // setHealth(Health - n)
    bool isDead() const { return m_dead; }
    // 死亡演出: 各ボディパーツを動的アクター化してランダムに吹き飛ばす（ラグドール）
    void enterRagdoll(Physics* physics);

    // --- アニメーション再生 ---
    // Animationインスタンスを再生する（先頭から）
    void playAnimation(std::shared_ptr<Animation> animation);
    void pauseAnimation();
    void stopAnimation();
    void setAnimationSpeed(float speed);
    bool isAnimationPlaying() const { return m_animPlaying; }

    // 毎フレーム呼び出し、再生中Animationのトラックを評価して対象Cubeのcframeを更新する
    void updateAnimation(float dt);

    // 一人称視点かどうかをUser側から渡し、身体パーツの透明化/復元を行う
    void updateFirstPersonState(bool wantsFirstPerson);

    bool isInFirstPerson() const { return isFirstPerson; }
    Vector3 getRootWorldPosition() const;
    Vector3 getHeadWorldPosition() const;

    // ボディパーツを Root の相対位置へ配置する（Free モードの追従でも使用）
    void applyBodyAnimation(bool leftArmRaised, bool rightArmRaised);

private:
    struct Pose {
        float leftArm;
        float rightArm;
        float leftLeg;
        float rightLeg;
    };

    float walkCycle = 0.0f;
    Vector3 currentMoveDir;
    bool isGrounded = true;
    bool m_dead = false;

    std::shared_ptr<Animation> m_currentAnim;
    float m_animTime = 0.0f;
    bool  m_animPlaying = false;

    bool isFirstPerson = false;
    bool bodyColorsSaved = false;
    Color4 savedTorsoColor;
    Color4 savedHeadColor;
    Color4 savedLeftArmColor;
    Color4 savedRightArmColor;
    Color4 savedLeftLegColor;
    Color4 savedRightLegColor;

    Pose computePose(bool leftArmRaised, bool rightArmRaised) const;
};
