#pragma once
#include <include/imgui/imgui.h>
#include <Editor/EditorPanel.hpp>
#include <Editor/CommandHistory.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/Instance.hpp>
#include <Core/User.hpp>
#include <cctype>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

class CommandHistory;
struct PickerState;  // PropertiesPanel.hpp で定義

// ===================================================
//  SceneHierarchyPanel  — ワークスペースのインスタンスツリーを表示
// ===================================================
class SceneHierarchyPanel : public EditorPanel {
public:
    Workspace*  workspace        = nullptr;
    Instance*   systemRoot       = nullptr;  // System ノード（Workspace の親）
    Instance*   selectedInstance  = nullptr;  // PropertiesPanel と共有（Primary）
    std::vector<Instance*> selectedInstances;  // 複数選択セット（常にselectedInstanceを含む）
    bool readOnly = false;

    // F2 インラインリネーム用状態（EditorManager::handleEditorShortcuts が設定する）
    Instance*   renamingInstance    = nullptr;
    bool        renameFocusPending  = false;

    CommandHistory*              m_history   = nullptr;
    std::vector<std::shared_ptr<Instance>>* m_clipboard = nullptr;  // EditorManager::m_clipboard へのポインタ（複数対応）
    User*                        m_user      = nullptr;
    PickerState*                 m_picker    = nullptr;  // Cube 参照ピック中はクリックを横取りする

    // Workspace 操作コールバック（main.cpp が設定）
    std::function<void(Workspace*)> onSwitchWorkspace;
    std::function<void(Workspace*)> onOpenSecondaryViewport;

    SceneHierarchyPanel();
    void onRender() override;

    // ツールバーの「New Script」ボタンから呼ばれる。Script(通常スクリプト)固定でダイアログを開く。
    void requestNewScript(const std::shared_ptr<Instance>& parent);

    // Ctrl+F: 選択インスタンスをツリー上で自動展開してスクロールする
    void requestReveal(Instance* inst);

    // ヘルパー: インスタンス名の重複を避けて連番を付ける
    // base の末尾が数字なら切り離してその数値からインクリメントする（"Cube1" -> "Cube2"）
    // taken: 同一バッチ内でまだ children に登録されていない予約済み名前の集合（複数ペースト用）
    static std::string uniqueName(const std::shared_ptr<Instance>& parent, const std::string& base,
                                   const std::unordered_set<std::string>* taken = nullptr) {
        auto exists = [&](const std::string& n) {
            return parent->children.count(n) > 0 || (taken && taken->count(n) > 0);
        };
        if (!exists(base)) return base;
        size_t i = base.size();
        while (i > 0 && std::isdigit((unsigned char)base[i - 1])) --i;
        std::string root = base.substr(0, i);
        int n = (i < base.size()) ? std::stoi(base.substr(i)) : 0;
        std::string name;
        do { name = root + std::to_string(++n); } while (exists(name));
        return name;
    }

    template <typename T, typename... Args>
    void tryInsertInstance(
        CommandHistory* history,
        const std::string& name,
        const std::shared_ptr<Instance>& parentSp,
        Args&&... args)
    {
        if (ImGui::MenuItem(name.c_str()) && history) {
            auto obj = std::make_shared<T>(std::forward<Args>(args)...);
            
            obj->Name = uniqueName(parentSp, name);
            history->execute(std::make_unique<AddInstanceCommand>(parentSp, obj));
        }
    }

private:
    // Script追加ダイアログ用(Script/LocalScript/ModuleScriptでダイアログを共用する)
    enum class ScriptInsertClass { Script, LocalScript, ModuleScript };
    std::shared_ptr<Instance> m_pendingScriptParent;
    bool                      m_openScriptDialog = false;
    ScriptInsertClass         m_pendingScriptClass = ScriptInsertClass::Script;
    std::shared_ptr<Instance> m_pendingTerrainParent;
    bool                      m_openTerrainDialog = false;
    bool                      m_doPickTerrain = false;
    bool                      m_pickExistingTerrain = false;
    std::string               m_pendingTerrainName;

    // ドラッグ＆ドロップの親変更は走査完了後にまとめて実行する
    // （drawNode が children マップを走査中に move すると iterator 無効化で表示が壊れるため）
    struct PendingReparent {
        std::shared_ptr<Instance> oldParent;
        std::shared_ptr<Instance> newParent;
        std::shared_ptr<Instance> child;
    };
    std::vector<PendingReparent> m_pendingReparents;
    Instance*                    m_pendingSelect = nullptr;

    // フォルダ/ファイル選択待ち（ピッカーはポップアップ外で実行）
    bool                      m_doPick      = false;
    bool                      m_pickExisting = false; // true=既存ファイル選択
    std::string               m_pickName;
    std::shared_ptr<Instance> m_pickParent;
    std::string               m_scriptDialogError;

    // Targets queued while a Script/Terrain group container dialog is open.
    std::vector<std::shared_ptr<Instance>> m_pendingGroupTargets;

    // F2 インラインリネーム用の編集バッファ（drawNode が再帰するためメンバで持つ）
    char m_renameBuf[256] = {};

    // Ctrl+F でリクエストされた自動展開・スクロール対象（requestReveal で設定）
    Instance* m_revealRequest = nullptr;

    void drawNode(Instance* inst);
    void renderInsertMenu(Instance* inst);
    void renderContextMenu(Instance* inst);
    void renderNewScriptDialog();
    void renderNewTerrainDialog();
};
