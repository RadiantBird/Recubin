#pragma once
#include <Editor/IEditorManager.hpp>
#include <include/GL/glew.h>
#include <include/GLFW/glfw3.h>
#include <include/imgui/imgui.h>
#include <include/imgui/imgui_impl_glfw.h>
#include <include/imgui/imgui_impl_opengl3.h>
#include <Core/Renderer.hpp>

// ランタイムビルド用の no-op 実装
// エディターなしで Renderer が常に有効な IEditorManager を持てるようにする
class NullEditorManager : public IEditorManager {
public:
    void beginViewportRender() override {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    void endViewportRender() override {}
    void render(GLFWwindow*) override {}
    void getViewportSize(GLFWwindow* window, int& w, int& h) override {
        glfwGetFramebufferSize(window, &w, &h);
    }
    Instance* getSelectedInstance() override { return nullptr; }
    void clearForImGui(GLFWwindow*) override {}

    void renderUI(User&, GLFWwindow* window, Workspace& ws) override {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

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
            Renderer::instance->renderScreenGui(ws, 0.f, 0.f, fw, fh);
            Renderer::instance->renderWorldGui (ws, 0.f, 0.f, fw, fh);
        }

        ImGui::End();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
};
