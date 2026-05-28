#pragma once
#include <include/imgui/imgui.h>
#include <Editor/EditorPanel.hpp>
#include <Editor/CommandHistory.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/Instance.hpp>
#include <Core/User.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class CommandHistory;

// ===================================================
//  SceneHierarchyPanel  — ワークスペースのインスタンスツリーを表示
// ===================================================
class SceneHierarchyPanel : public EditorPanel {
public:
    Workspace*  workspace        = nullptr;
    Instance*   systemRoot       = nullptr;  // System ノード（Workspace の親）
    Instance*   selectedInstance  = nullptr;  // PropertiesPanel と共有（Primary）
    std::vector<Instance*> selectedInstances;  // 複数選択セット（常にselectedInstanceを含む）

    CommandHistory*              m_history   = nullptr;
    std::shared_ptr<Instance>*   m_clipboard = nullptr;  // EditorManager::m_clipboard へのポインタ
    User*                        m_user      = nullptr;

    // Workspace 操作コールバック（main.cpp が設定）
    std::function<void(Workspace*)> onSwitchWorkspace;
    std::function<void(Workspace*)> onOpenSecondaryViewport;

    SceneHierarchyPanel();
    void onRender() override;

    // ヘルパー: インスタンス名の重複を避けて連番を付ける
    static std::string uniqueName(const std::shared_ptr<Instance>& parent, const std::string& base) {
        std::string name = base;
        int n = 1;
        while (parent->children.count(name) > 0)
            name = base + std::to_string(n++);
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
    // Script追加ダイアログ用
    std::shared_ptr<Instance> m_pendingScriptParent;
    bool                      m_openScriptDialog = false;

    // フォルダ/ファイル選択待ち（ピッカーはポップアップ外で実行）
    bool                      m_doPick      = false;
    bool                      m_pickExisting = false; // true=既存ファイル選択
    std::string               m_pickName;
    std::shared_ptr<Instance> m_pickParent;

    void drawNode(Instance* inst);
    void renderInsertMenu(Instance* inst);
    void renderContextMenu(Instance* inst);
    void renderNewScriptDialog();
};
