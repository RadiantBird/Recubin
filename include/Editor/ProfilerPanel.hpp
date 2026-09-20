#pragma once

#include <Editor/EditorPanel.hpp>
#include <include/imgui/imgui.h>

class ProfilerPanel final : public EditorPanel {
public:
    ImGuiID dockspaceId = 0;

    ProfilerPanel();
    void onRender() override;
};
