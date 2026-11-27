#pragma once

#include <Editor/EditorPanel.hpp>
#include <include/imgui/imgui.h>
#include <functional>
#include <string>

class System;

class CollisionGroupsPanel final : public EditorPanel {
public:
    ImGuiID dockspaceId = 0;

    CollisionGroupsPanel() : EditorPanel("Collision Groups") {}
    void setSystem(System* system) { m_system = system; }
    void setOnChanged(std::function<void()> callback) { m_onChanged = std::move(callback); }
    void onRender() override;

private:
    System* m_system = nullptr;
    std::function<void()> m_onChanged;
    char m_newGroupName[128] = {};
    std::string m_statusMessage;
};
