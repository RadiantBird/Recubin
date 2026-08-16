#pragma once
#include <Editor/EditorPanel.hpp>
#include <Instances/Instance.hpp>
#include <Instances/Animation.hpp>
#include <Instances/Humanoid.hpp>
#include <Math/CFrame.hpp>
#include <string>
#include <unordered_map>

class CommandHistory;

// ===================================================
//  AnimationEditorPanel
//   選択中Modelの子Animationを編集する。再生バーで時間tを設定し、
//   子Cubeを選択→ギズモで動かして「キー追加」で現在CFrameを記録する。
//   「編集開始」ボタンでModel配下Cubeの元CFrameを退避してリグを組み立て、
//   「編集終了」またはPlay開始/シーン再読込で復元する（明示的な編集セッション）。
// ===================================================
class AnimationEditorPanel : public EditorPanel {
public:
    Instance**      selectedInstance = nullptr; // SceneHierarchy と共有
    CommandHistory* m_history        = nullptr; // Animation生成をundo可能にする

    AnimationEditorPanel();
    void onRender() override;
    void endEditSession();

private:
    float m_time         = 0.0f;
    bool  m_playing      = false;
    int   m_easingChoice = 0;     // EasingType に対応するコンボ選択

    bool       m_editing    = false; // 編集セッション中か。開始ボタンで退避+リグ組み立て、終了で復元
    bool       m_showFileError = false;
    std::string m_fileError;
    bool       m_poseSaved  = false;
    Instance*  m_savedModel = nullptr;
    std::unordered_map<std::string, CFrame> m_bindPose; // partName -> 退避CFrame

    // 選択インスタンスから編集対象のModelを求める
    Instance* resolveModel() const;
    // model直下の最初のAnimationを返す（なければnullptr）
    Animation* findAnimation(Instance* model) const;
    // model直下の最初のHumanoidを返す（なければnullptr）
    Humanoid* findHumanoid(Instance* model) const;

    void saveBindPose(Instance* model);
    void restoreBindPose();
    void applyPreview(Animation* anim, Instance* model, float t);
};
