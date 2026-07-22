#pragma once
#include <Editor/IEditorManager.hpp>
#include <include/GL/glew.h>
#include <include/GLFW/glfw3.h>
#include <include/imgui/imgui.h>
#include <include/imgui/imgui_impl_glfw.h>
#include <include/imgui/imgui_impl_opengl3.h>
#include <Core/Renderer.hpp>
#include <Editor/SceneHierarchyPanel.hpp>
#include <Editor/PropertiesPanel.hpp>
#include <algorithm>

// ランタイムビルド用の no-op 実装
// エディターなしで Renderer が常に有効な IEditorManager を持てるようにする
class NullEditorManager : public IEditorManager {
    bool m_debugVisible = false;
    SceneHierarchyPanel m_hierarchy;
    PropertiesPanel m_properties;
public:
    NullEditorManager() {
        m_hierarchy.readOnly = true;
        m_properties.readOnly = true;
        m_properties.selectedInstance = &m_hierarchy.selectedInstance;
        m_properties.selectedInstances = &m_hierarchy.selectedInstances;
    }

    bool isDebugCapturingKeyboard() const { return m_debugVisible; }
    void beginViewportRender() override {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    void endViewportRender() override {}
    void render(GLFWwindow*) override {}
    void getViewportSize(GLFWwindow* window, int& w, int& h) override {
        glfwGetFramebufferSize(window, &w, &h);
    }
    unsigned int getViewportFBO() override { return 0; }
    bool isViewportFocused() override { return true; }
    Instance* getSelectedInstance() override {
        return m_debugVisible ? m_hierarchy.selectedInstance : nullptr;
    }
    void clearForImGui(GLFWwindow*) override {}
    bool ownsSceneRender() override { return false; }


    void renderUI(User& user, GLFWwindow* window, Workspace& ws) override {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const bool chatCapturing = Renderer::instance && Renderer::instance->isChatCapturingKeyboard();
        if (!chatCapturing && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F3, false)) {
            m_debugVisible = !m_debugVisible;
            if (m_debugVisible) {
                m_hierarchy.isOpen = true;
                m_properties.isOpen = true;
            }
        }

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        float fw = (float)w, fh = (float)h;

        // ゲーム GUI 描画用フルスクリーン透明ウィンドウ
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(fw, fh));
        ImGui::SetNextWindowBgAlpha(0.f);
        ImGui::Begin("##GameGui", nullptr,
            ImGuiWindowFlags_NoDecoration   |
            ImGuiWindowFlags_NoNav          |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        if (Renderer::instance) {
            Renderer::instance->renderGameGui(ws, &user, 0.f, 0.f, fw, fh);
            Renderer::instance->renderRuntimeChat(0.f, 0.f, fw, fh);
        }

        ImGui::End();

        if (m_debugVisible) {
            m_hierarchy.workspace = &ws;
            Instance* root = &ws;
            for (auto parent = root->Parent.lock(); parent; parent = parent->Parent.lock()) {
                root = parent.get();
            }
            m_hierarchy.systemRoot = root;

            ImGui::SetNextWindowPos(ImVec2(std::max(8.f, fw - 600.f), 16.f), ImGuiCond_FirstUseEver);
            m_hierarchy.onRender();
            ImGui::SetNextWindowPos(ImVec2(std::max(268.f, fw - 340.f), 16.f), ImGuiCond_FirstUseEver);
            m_properties.onRender();
        }

        if (Renderer::instance) Renderer::instance->drawCameraRotationCursor(user, window);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
};
