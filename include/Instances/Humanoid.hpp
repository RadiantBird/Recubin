#pragma once

#include <Instances/Instance.hpp>
#include <Instances/Cube.hpp>
#include <Instances/Sphere.hpp>
#include <Util/Color4.hpp>
#include <Core/RCBNScriptSignal.hpp>

class Physics;   // Forward declaration
class Animation; // Forward declaration
class Seat;      // Forward declaration
class Weld;      // Forward declaration

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
    /*
        @RadiantBird
        2026/07/19:
        暗黙的にm基準だったころのデフォルト値で、
        まともに操作できないのでstud基準での標準値に変更しました。
    */ 
    float WalkSpeed  = 16.0f;
    float JumpPower  = 48.522f; // sqrt(2gh) ... sqrt(2*196.2*6) = 48.522
    float ClimbSpeed = 10.0f; // Truss(はしご)接触中の垂直移動速度

    // JumpPowerから逆算した跳躍到達高さ(stud)。設定するとJumpPowerが自動計算される
    float getJumpHeight() const;
    void  setJumpHeight(float height);

    // --- ヘルス ---
    float Health      = 100.0f;
    float MaxHealth   = 100.0f;
    float RespawnTime = 5.0f;   // 死亡後この秒数で再生成される
    std::shared_ptr<RCBNScriptSignal> Died; // Health<=0 で1度だけ発火
    std::shared_ptr<RCBNScriptSignal> KeyframeReached; // (partName: string, time: number) で発火

    // 兄弟パーツへの参照（resolveParts()で解決する）
    // TODO: Weld/Motor/Rope/Rodは兄弟BaseCubeをweak_ptrで参照する規約だが、Humanoidだけ
    // shared_ptr(強参照)。Play中も残り続けるテンプレートキャラクター等で通常の破棄経路を
    // 通らない場合、actorの解放漏れと組み合わさると危険。将来的にweak_ptr化を検討。
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
    // forwardAxis/rightAxis: W:+1/S:-1, A:-1/D:+1の生の独立2軸(-1..1)。Truss登坂とSeat.Steer/Throttleで使う
    void move(const Vector3& flatForward, const Vector3& flatRight, bool isPressingMove,
              const Vector3& targetMoveDir, bool ctrlLockEnabled, Physics* physics,
              bool leftArmRaised = false, bool rightArmRaised = false,
              float forwardAxis = 0.0f, float rightAxis = 0.0f);

    // 接地中、または水没中にJumpPowerで上方向の速度をセットする
    void jump(Physics* physics);

    // Seatに着席中か。着席中はUser側の通常移動の代わりにSeat.Steer/Throttle更新のみ行われる
    bool isSeated() const { return m_seated; }
    // Space押下時、着席中ならjump()の代わりにこちらが呼ばれ、Weldを解除して離脱する
    void standUp(Physics* physics);

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

    // メインループから毎フレーム1回だけ呼ぶ。ツリーを再帰的に辿り、見つけた全Humanoidの
    // updateAnimation(dt)を呼ぶ(ParticleEmitter::updateAllと同じ木構造走査パターン)
    static void updateAll(Instance* root, float dt);

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

    // --- Seat(着席) ---
    bool m_seated = false;
    std::weak_ptr<Seat> m_seat;
    std::shared_ptr<Weld> m_seatWeld;
    // Root接触中のSeatへ着席し、Weldで固定する(move()内、未着席時のみ呼ばれる)
    void sitOn(std::shared_ptr<Seat> seat, Physics* physics);

    std::shared_ptr<Animation> m_currentAnim;
    float m_animTime = 0.0f;
    bool  m_animPlaying = false;
    bool  m_bodyPoseUpdatedThisFrame = false; // このフレームでapplyBodyAnimation()が(呼び出し元を問わず)実行されたか。
                                               // updateAnimation()冒頭で毎フレーム消費し、falseならアイドルポーズへフォールバックする

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
