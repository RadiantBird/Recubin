#pragma once
#include <Editor/IEditorManager.hpp>
#include <include/GL/glew.h>
#include <include/GLFW/glfw3.h>

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
    void renderUI(User&, GLFWwindow*, Workspace&) override {}
};
