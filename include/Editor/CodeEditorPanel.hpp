#pragma once

#include <Editor/EditorPanel.hpp>

#include <memory>
#include <string>
#include <vector>

class Instance;
class RuntimeFileSystem;
struct ImFont;
struct ImGuiInputTextCallbackData;
using ImGuiID = unsigned int;

// A small dockable source/text editor used by the editor's auxiliary panels.
class CodeEditorPanel final : public EditorPanel {
public:
    CodeEditorPanel(std::shared_ptr<Instance> target, ImGuiID dockspaceId, ImFont* codeFont,
                    RuntimeFileSystem* runtimeFileSystem = nullptr);

    void onRender() override;

    std::shared_ptr<Instance> target() const;
    bool isDirty() const;
    bool isFocused() const;
    bool wantsClose() const;
    void clearCloseRequest();
    void setDockspaceId(ImGuiID dockspaceId);
    void setOpen(bool open);
    bool save();

    struct HighlightSpan {
        int begin = 0;
        int end = 0;
        unsigned int color = 0;
    };

private:
    std::weak_ptr<Instance> m_target;
    ImGuiID m_dockspaceId = 0;
    ImFont* m_codeFont = nullptr;
    float m_codeFontSize = 17.0f;
    RuntimeFileSystem* m_runtimeFileSystem = nullptr;
    std::string m_text;
    std::string m_loadedText;
    std::vector<HighlightSpan> m_spans;
    std::vector<int> m_lineStarts;
    bool m_dirty = false;
    bool m_closeRequested = false;
    bool m_confirmClose = false;
    double m_confirmCloseOpenedAt = 0.0;
    bool m_focused = false;
    bool m_loaded = false;

    static int resizeInputCallback(ImGuiInputTextCallbackData* data);
    void loadTarget();
    void rebuildHighlightCache();
    void drawHighlightedText(struct ImGuiWindow* parent, unsigned int inputId,
                             float gutterWidth);
    void drawCloseConfirmation();
};
