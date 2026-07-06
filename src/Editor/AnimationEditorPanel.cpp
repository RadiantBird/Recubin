#include <Editor/AnimationEditorPanel.hpp>
#include <Editor/CommandHistory.hpp>
#include <Instances/Spatial.hpp>
#include <include/imgui/imgui.h>
#include <Util/Platform.hpp>
#include <Util/IPlatform.hpp>
#include <string>

namespace {

std::string openAnimDialog(bool save) {
    static const std::vector<FileFilter> filters = {{"Animation (*.yaml)", "*.yaml"}};
    return save ? getPlatform().saveFileDialog(filters, "yaml")
                : getPlatform().openFileDialog(filters);
}

} // namespace

AnimationEditorPanel::AnimationEditorPanel() : EditorPanel("Animation Editor") {}

Instance* AnimationEditorPanel::resolveModel() const {
    Instance* sel = selectedInstance ? *selectedInstance : nullptr;
    if (!sel) return nullptr;
    if (sel->getClassName() == "Animation") return sel->Parent.lock().get();
    if (sel->IsA("Model"))   return sel;
    if (sel->IsA("Spatial")) return sel->Parent.lock().get();
    return nullptr;
}

Animation* AnimationEditorPanel::findAnimation(Instance* model) const {
    if (!model) return nullptr;
    for (auto const& [name, child] : model->getChildren()) {
        if (child->getClassName() == "Animation")
            return static_cast<Animation*>(child.get());
    }
    return nullptr;
}

void AnimationEditorPanel::saveBindPose(Instance* model) {
    m_bindPose.clear();
    if (model) {
        for (auto const& [name, child] : model->getChildren()) {
            if (auto* sp = dynamic_cast<Spatial*>(child.get()))
                m_bindPose[name] = sp->cframe;
        }
    }
    m_poseSaved  = true;
    m_savedModel = model;
}

void AnimationEditorPanel::restoreBindPose() {
    if (m_poseSaved && m_savedModel) {
        for (auto const& [name, cf] : m_bindPose) {
            if (auto* sp = dynamic_cast<Spatial*>(m_savedModel->getChild(name)))
                sp->cframe = cf;
        }
    }
    m_bindPose.clear();
    m_poseSaved  = false;
    m_savedModel = nullptr;
}

void AnimationEditorPanel::applyPreview(Animation* anim, Instance* model, float t) {
    if (!anim || !model) return;
    // キーフレームはRoot相対なので、現在のRoot CFrameに合成して適用する
    Spatial* root = dynamic_cast<Spatial*>(model->getChild("Root"));
    CFrame rootCF = root ? root->cframe : CFrame();
    for (const AnimTrack& track : anim->getTracks()) {
        Spatial* sp = dynamic_cast<Spatial*>(model->getChild(track.partName));
        if (!sp || sp == root) continue;
        sp->cframe = rootCF * anim->evaluateTrack(track, t);
    }
}

void AnimationEditorPanel::onRender() {
    ImGui::SetNextWindowSize(ImVec2(560, 320), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title.c_str(), &isOpen)) {
        ImGui::End();
        return;
    }

    Instance* model = resolveModel();
    Animation* anim = model ? findAnimation(model) : nullptr;

    // --- フォーカス遷移によるバインドポーズの退避/復元 ---
    bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (focused && model && m_poseSaved && m_savedModel != model) {
        restoreBindPose();          // 対象Modelが変わったら旧Modelを復元
    }
    if (focused && !m_poseSaved && model) {
        saveBindPose(model);
    }
    if (!focused && m_wasFocused) {
        restoreBindPose();
        m_playing = false;
    }
    m_wasFocused = focused;

    // --- 対象が無い場合の案内 ---
    if (!model) {
        ImGui::TextWrapped("HierarchyでキャラクターのModelまたはその子Cubeを選択してください。");
        ImGui::End();
        return;
    }

    ImGui::Text("Target Model: %s", model->Name.c_str());

    // --- Animationが無ければ作成ボタン ---
    if (!anim) {
        ImGui::Spacing();
        if (ImGui::Button("Create Animation") && m_history) {
            auto animSp = std::make_shared<Animation>();
            animSp->Name = "Animation";
            m_history->execute(std::make_unique<AddInstanceCommand>(
                model->shared_from_this(), animSp));
        }
        ImGui::End();
        return;
    }

    // --- 再生コントロール ---
    const ImVec2 btnSz = ImVec2(70.0f, 30.0f);
    if (m_playing) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.55f, 0.0f, 1.0f));
        if (ImGui::Button("Pause", btnSz)) m_playing = false;
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.65f, 0.18f, 1.0f));
        if (ImGui::Button("Play", btnSz)) m_playing = true;
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.18f, 0.18f, 1.0f));
    if (ImGui::Button("Stop", btnSz)) {
        m_playing = false;
        m_time = 0.0f;
        applyPreview(anim, model, m_time);
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::DragFloat("Speed", &anim->Speed, 0.05f, 0.05f, 10.0f, "%.2fx");

    // --- 専用ファイルへのエクスポート/インポート ---
    if (ImGui::Button("Export...")) {
        std::string path = openAnimDialog(true);
        if (!path.empty()) anim->exportToFile(path);
    }
    ImGui::SameLine();
    if (ImGui::Button("Import...")) {
        std::string path = openAnimDialog(false);
        if (!path.empty()) anim->importFromFile(path);
    }

    // --- 長さ・再生バー ---
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("Length", &anim->Length, 0.05f, 0.1f, 600.0f, "%.2f s");
    if (m_time > anim->Length) m_time = anim->Length;

    bool timeChanged = ImGui::SliderFloat("Time", &m_time, 0.0f, anim->Length, "%.2f s");

    // --- キー記録（対象Cubeが選択されているとき）---
    Instance* sel = selectedInstance ? *selectedInstance : nullptr;
    Spatial* keyPart = nullptr;
    if (sel && sel->IsA("Spatial") && sel->Parent.lock().get() == model)
        keyPart = dynamic_cast<Spatial*>(sel);

    ImGui::SeparatorText("Keyframe");
    const char* easingNames[] = { "Linear", "Quadratic", "Cosine", "Sine", "Exponential" };
    ImGui::SetNextItemWidth(140.0f);
    ImGui::Combo("Easing", &m_easingChoice, easingNames, IM_ARRAYSIZE(easingNames));
    ImGui::SameLine();
    if (keyPart) {
        if (ImGui::Button("Add Key")) {
            // Cubeの現在ローカルCFrameをRoot相対に変換して記録する
            Spatial* root = dynamic_cast<Spatial*>(model->getChild("Root"));
            CFrame rel = keyPart->cframe;
            if (root && keyPart != root)
                rel = root->cframe.inverse() * keyPart->cframe;
            anim->addOrReplaceKey(keyPart->Name, m_time, rel,
                                  static_cast<EasingType>(m_easingChoice));
            if (m_time > anim->Length) anim->Length = m_time;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("part: %s", keyPart->Name.c_str());
    } else {
        ImGui::TextDisabled("Cube(Model直下)を選択するとキーを追加できます");
    }

    // --- トラック一覧 ---
    ImGui::SeparatorText("Tracks");
    if (ImGui::BeginChild("TrackList", ImVec2(0, 0), true)) {
        for (AnimTrack& track : anim->getTracks()) {
            if (ImGui::TreeNode(track.partName.c_str(),
                                "%s  (%d keys)", track.partName.c_str(),
                                static_cast<int>(track.keyframes.size()))) {
                for (size_t i = 0; i < track.keyframes.size(); ++i) {
                    Keyframe& kf = track.keyframes[i];
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::Text("t=%.2f", kf.time);
                    ImGui::SameLine();
                    int e = static_cast<int>(kf.easing);
                    ImGui::SetNextItemWidth(120.0f);
                    if (ImGui::Combo("##e", &e, easingNames, IM_ARRAYSIZE(easingNames)))
                        kf.easing = static_cast<EasingType>(e);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Go")) {
                        m_time = kf.time;
                        applyPreview(anim, model, m_time);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) {
                        anim->removeKey(track.partName, kf.time);
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
        }
    }
    ImGui::EndChild();

    // --- プレビュー適用（再生中、または再生バーをドラッグしたフレームのみ）---
    if (m_playing) {
        m_time += ImGui::GetIO().DeltaTime * anim->Speed;
        if (anim->Length > 1e-6f) {
            while (m_time > anim->Length) m_time -= anim->Length;
        }
        applyPreview(anim, model, m_time);
    } else if (timeChanged) {
        applyPreview(anim, model, m_time);
    }

    ImGui::End();
}
