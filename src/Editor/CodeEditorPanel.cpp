#include <Editor/CodeEditorPanel.hpp>

#include <Core/FileLoader.hpp>
#include <Editor/Localization.hpp>
#include <Editor/UiHelpers.hpp>
#include <Instances/Instance.hpp>
#include <Instances/Script.hpp>
#include <Instances/TextFile.hpp>
#include <Util/AssetPath.hpp>
#include <Util/AssetGuard.hpp>
#include <Util/Logger.hpp>
#include <Util/RuntimeFileSystem.hpp>
#include <include/imgui/imgui.h>
#include <include/imgui/imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {
enum class LexState { Normal, BlockComment, LongString };

// Keep the text overlay aligned with the padding used by InputTextMultiline.
// The overlay is drawn after the temporary ImGui style push is popped, so it
// must not derive its origin from the current global FramePadding.
constexpr float CODE_EDITOR_GUTTER_WIDTH = 48.0f;
constexpr float CODE_EDITOR_FRAME_LEFT_PADDING = 8.0f;
constexpr float CODE_EDITOR_FRAME_TOP_PADDING = 6.0f;
constexpr float CODE_EDITOR_DEFAULT_FONT_SIZE = 17.0f;
constexpr float CODE_EDITOR_MIN_FONT_SIZE = 10.0f;
constexpr float CODE_EDITOR_MAX_FONT_SIZE = 34.0f;
constexpr float CODE_EDITOR_FONT_SIZE_STEP = 1.0f;

ImVec4 colorForToken(int kind) {
    switch (kind) {
    case 1: return ImVec4(0.40f, 0.72f, 1.00f, 1.0f); // keyword
    case 2: return ImVec4(0.98f, 0.76f, 0.38f, 1.0f); // number
    case 3: return ImVec4(0.73f, 0.92f, 0.56f, 1.0f); // string
    case 4: return ImVec4(0.42f, 0.67f, 0.54f, 1.0f); // comment
    case 5: return ImVec4(0.74f, 0.57f, 0.96f, 1.0f); // type
    case 6: return ImVec4(0.82f, 0.86f, 0.92f, 1.0f); // operator
    case 7: return ImVec4(0.54f, 0.86f, 0.88f, 1.0f); // literal
    default: return ImVec4(0.82f, 0.84f, 0.88f, 1.0f);
    }
}

bool isIdentifierStart(unsigned char c) { return std::isalpha(c) || c == '_'; }
bool isIdentifierChar(unsigned char c) { return std::isalnum(c) || c == '_'; }

bool isKeyword(std::string_view word) {
    static constexpr std::string_view words[] = {
        "and", "break", "do", "else", "elseif", "end", "false", "for", "function",
        "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true",
        "until", "while", "continue", "export", "global", "type"
    };
    return std::find(std::begin(words), std::end(words), word) != std::end(words);
}

bool isLiteral(std::string_view word) {
    return word == "true" || word == "false" || word == "nil";
}

bool isTypeName(std::string_view word) {
    static constexpr std::string_view words[] = {
        "any", "boolean", "number", "string", "thread", "table", "userdata", "vector",
        "Cube", "Vector2", "Vector3", "CFrame", "Color4", "Instance", "Model", "Part"
    };
    return std::find(std::begin(words), std::end(words), word) != std::end(words);
}

void addSpan(std::vector<CodeEditorPanel::HighlightSpan>& spans, int begin, int end,
             ImU32 color) {
    if (end > begin) spans.push_back({begin, end, color});
}
}

CodeEditorPanel::CodeEditorPanel(std::shared_ptr<Instance> target, ImGuiID dockspaceId,
                                 ImFont* codeFont, RuntimeFileSystem* runtimeFileSystem)
    : EditorPanel("Code Editor"), m_target(std::move(target)), m_dockspaceId(dockspaceId),
      m_codeFont(codeFont),
      m_codeFontSize(codeFont != nullptr && codeFont->LegacySize > 0.0f
                         ? codeFont->LegacySize : CODE_EDITOR_DEFAULT_FONT_SIZE),
      m_runtimeFileSystem(runtimeFileSystem) {
    auto instance = m_target.lock();
    title = instance ? instance->Name : "Code Editor";
    isOpen = instance != nullptr;
    loadTarget();
}

std::shared_ptr<Instance> CodeEditorPanel::target() const { return m_target.lock(); }
bool CodeEditorPanel::isDirty() const { return m_dirty; }
bool CodeEditorPanel::isFocused() const { return m_focused; }
bool CodeEditorPanel::wantsClose() const { return m_closeRequested; }
void CodeEditorPanel::clearCloseRequest() { m_closeRequested = false; }
void CodeEditorPanel::setDockspaceId(ImGuiID dockspaceId) { m_dockspaceId = dockspaceId; }
void CodeEditorPanel::setOpen(bool open) { isOpen = open; }

void CodeEditorPanel::loadTarget() {
    auto instance = m_target.lock();
    if (!instance) return;

    if (auto script = std::dynamic_pointer_cast<Script>(instance)) {
        m_text = script->Source;
    } else if (auto textFile = std::dynamic_pointer_cast<TextFile>(instance)) {
        if (m_runtimeFileSystem != nullptr) {
            const RuntimeFileResult result = m_runtimeFileSystem->readTextFile(textFile->Path, textFile->StorageId);
            if (result.success) {
                m_text = result.value;
            } else {
                RCBN_ERROR("CodeEditorPanel: failed to read TextFile " << textFile->getFullPath()
                           << " (path=" << textFile->Path << "): " << result.error);
            }
        } else {
            m_text = textFile->Path.empty() ? std::string{} : FileLoader::readText(textFile->Path);
        }
    } else {
        std::cerr << "[CodeEditorPanel] Unsupported editor target: " << instance->Name << '\n';
        isOpen = false;
        return;
    }

    m_loadedText = m_text;
    m_loaded = true;
    m_dirty = false;
    rebuildHighlightCache();
}

int CodeEditorPanel::resizeInputCallback(ImGuiInputTextCallbackData* data) {
    if (data == nullptr || data->UserData == nullptr) return 0;
    auto* text = static_cast<std::string*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        text->resize(static_cast<size_t>(data->BufTextLen));
        data->Buf = text->data();
    }
    return 0;
}

void CodeEditorPanel::rebuildHighlightCache() {
    m_spans.clear();
    m_lineStarts.clear();
    m_lineStarts.push_back(0);
    const ImU32 normal = ImGui::GetColorU32(colorForToken(0));
    const ImU32 keyword = ImGui::GetColorU32(colorForToken(1));
    const ImU32 number = ImGui::GetColorU32(colorForToken(2));
    const ImU32 string = ImGui::GetColorU32(colorForToken(3));
    const ImU32 comment = ImGui::GetColorU32(colorForToken(4));
    const ImU32 type = ImGui::GetColorU32(colorForToken(5));
    const ImU32 oper = ImGui::GetColorU32(colorForToken(6));
    const ImU32 literal = ImGui::GetColorU32(colorForToken(7));
    LexState state = LexState::Normal;

    auto recordLineBreaks = [&](size_t begin, size_t end) {
        for (size_t newline = m_text.find('\n', begin);
             newline < end && newline != std::string::npos;
             newline = m_text.find('\n', newline + 1)) {
            m_lineStarts.push_back(static_cast<int>(newline + 1));
        }
    };

    size_t i = 0;
    while (i < m_text.size()) {
        if (m_text[i] == '\n') { m_lineStarts.push_back(static_cast<int>(i + 1)); ++i; continue; }
        const int begin = static_cast<int>(i);
        if (state == LexState::BlockComment || state == LexState::LongString) {
            const std::string_view endMarker = "]]";
            const size_t end = m_text.find(endMarker, i);
            const size_t finish = end == std::string::npos ? m_text.size() : end + 2;
            addSpan(m_spans, begin, static_cast<int>(finish),
                    state == LexState::BlockComment ? comment : string);
            recordLineBreaks(i, finish);
            i = finish;
            if (end != std::string::npos) state = LexState::Normal;
            continue;
        }
        if (m_text.compare(i, 4, "--[[") == 0) {
            state = LexState::BlockComment;
            continue;
        }
        if (m_text.compare(i, 2, "[[") == 0) {
            state = LexState::LongString;
            continue;
        }
        if (m_text.compare(i, 2, "--") == 0) {
            const size_t end = m_text.find('\n', i);
            addSpan(m_spans, begin, static_cast<int>(end == std::string::npos ? m_text.size() : end), comment);
            i = end == std::string::npos ? m_text.size() : end;
            continue;
        }
        if (m_text[i] == '"' || m_text[i] == '\'') {
            const char quote = m_text[i++];
            bool escaped = false;
            while (i < m_text.size()) {
                if (!escaped && m_text[i] == quote) { ++i; break; }
                escaped = !escaped && m_text[i] == '\\';
                if (m_text[i] != '\\') escaped = false;
                ++i;
            }
            addSpan(m_spans, begin, static_cast<int>(i), string);
            recordLineBreaks(begin, i);
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(m_text[i]))) {
            ++i;
            while (i < m_text.size() && (std::isalnum(static_cast<unsigned char>(m_text[i])) || m_text[i] == '.' || m_text[i] == '_')) ++i;
            addSpan(m_spans, begin, static_cast<int>(i), number);
            continue;
        }
        if (isIdentifierStart(static_cast<unsigned char>(m_text[i]))) {
            ++i;
            while (i < m_text.size() && isIdentifierChar(static_cast<unsigned char>(m_text[i]))) ++i;
            const std::string_view word(m_text.data() + begin, i - static_cast<size_t>(begin));
            if (isLiteral(word)) addSpan(m_spans, begin, static_cast<int>(i), literal);
            else if (isKeyword(word)) addSpan(m_spans, begin, static_cast<int>(i), keyword);
            else if (isTypeName(word)) addSpan(m_spans, begin, static_cast<int>(i), type);
            continue;
        }
        if (std::string_view("+-*/%=<>~^:,.;(){}[]#|&").find(m_text[i]) != std::string_view::npos) {
            addSpan(m_spans, begin, begin + 1, oper);
        }
        ++i;
    }
    (void)normal;
}

void CodeEditorPanel::drawHighlightedText(ImGuiWindow* parent, unsigned int inputId,
                                           float gutterWidth) {
    ImGuiWindow* child = nullptr;
    for (int i = parent->DC.ChildWindows.Size - 1; i >= 0; --i) {
        ImGuiWindow* candidate = parent->DC.ChildWindows[i];
        if (candidate != nullptr && candidate->ChildId == inputId) { child = candidate; break; }
    }
    if (!child) return;
    const float bodyOriginX = child->DC.CursorStartPos.x + gutterWidth +
        CODE_EDITOR_FRAME_LEFT_PADDING;
    ImVec2 origin(bodyOriginX, child->DC.CursorStartPos.y + CODE_EDITOR_FRAME_TOP_PADDING);
    if (ImGuiInputTextState* state = ImGui::GetInputTextState(inputId); state != nullptr) {
        origin.x -= state->Scroll.x;
    }
    const float lineHeight = ImGui::GetFontSize();
    const ImRect clip = child->InnerClipRect;
    const ImVec4 clipRect = clip.ToVec4();
    child->DrawList->AddRectFilled(
        ImVec2(bodyOriginX - gutterWidth, clip.Min.y), ImVec2(bodyOriginX - 4.0f, clip.Max.y),
        ImGui::GetColorU32(ImVec4(0.035f, 0.055f, 0.095f, 1.0f)));
    const int lineCount = std::max(1, static_cast<int>(m_lineStarts.size()));
    const int firstLine = std::max(0, static_cast<int>(std::floor((clip.Min.y - origin.y) / lineHeight)));
    const int lastLine = std::min(lineCount, static_cast<int>(std::ceil((clip.Max.y - origin.y) / lineHeight)) + 1);
    for (int line = firstLine; line < lastLine; ++line) {
        const std::string number = std::to_string(line + 1);
        const float numberWidth = ImGui::CalcTextSize(number.c_str()).x;
        child->DrawList->AddText(m_codeFont ? m_codeFont : ImGui::GetFont(), ImGui::GetFontSize(),
            ImVec2(bodyOriginX - 8.0f - numberWidth, origin.y + line * lineHeight),
            ImGui::GetColorU32(ImVec4(0.38f, 0.48f, 0.61f, 1.0f)), number.c_str(), nullptr, 0.0f, &clipRect);
        const int lineStart = m_lineStarts[line];
        const int lineEnd = line + 1 < lineCount ? m_lineStarts[line + 1] : static_cast<int>(m_text.size());
        ImFont* font = m_codeFont ? m_codeFont : ImGui::GetFont();
        const float fontSize = ImGui::GetFontSize();
        const std::string_view lineText(m_text.data() + lineStart,
                                        static_cast<size_t>(std::max(lineStart, lineEnd) - lineStart));
        child->DrawList->AddText(font, fontSize,
            ImVec2(origin.x, origin.y + line * lineHeight),
            ImGui::GetColorU32(colorForToken(0)), lineText.data(), lineText.data() + lineText.size(),
            0.0f, &clipRect);
        for (const auto& span : m_spans) {
            const int begin = std::max(span.begin, lineStart);
            const int end = std::min(span.end, lineEnd);
            if (begin >= end) continue;
            const std::string_view part(m_text.data() + begin, static_cast<size_t>(end - begin));
            const std::string_view prefix(m_text.data() + lineStart,
                                          static_cast<size_t>(begin - lineStart));
            const float prefixWidth = font->CalcTextSizeA(
                fontSize, FLT_MAX, 0.0f, prefix.data(), prefix.data() + prefix.size()).x;
            const float x = origin.x + prefixWidth;
            child->DrawList->AddText(font, fontSize,
                ImVec2(x, origin.y + line * lineHeight), span.color,
                part.data(), part.data() + part.size(), 0.0f, &clipRect);
        }
    }
}

bool CodeEditorPanel::save() {
    auto instance = m_target.lock();
    if (!instance) return false;
    if (!m_dirty) return true;
    if (auto script = std::dynamic_pointer_cast<Script>(instance)) {
        if (!script->Path.empty()) {
            if (script->isPrecompiled || script->Path.ends_with(".luauc")) {
                RCBN_ERROR("CodeEditorPanel: refusing to overwrite precompiled Script "
                           << script->getFullPath() << " (path=" << script->Path << ")");
                return false;
            }
            if (!AssetGuard::allow(script->Path)) {
                RCBN_ERROR("CodeEditorPanel: Script path is not allowed: " << script->Path);
                return false;
            }
            std::ofstream output(AssetPath::fromStored(script->Path),
                                 std::ios::binary | std::ios::trunc);
            if (!output.is_open()) {
                RCBN_ERROR("CodeEditorPanel: failed to write Script " << script->getFullPath()
                           << " (path=" << script->Path << ")");
                return false;
            }
            output.write(m_text.data(), static_cast<std::streamsize>(m_text.size()));
            if (!output.good()) {
                RCBN_ERROR("CodeEditorPanel: failed while writing Script " << script->getFullPath()
                           << " (path=" << script->Path << ")");
                return false;
            }
        }
        script->Source = m_text;
    } else if (auto textFile = std::dynamic_pointer_cast<TextFile>(instance)) {
        if (textFile->Path.empty()) return false;
        if (m_runtimeFileSystem != nullptr) {
            const RuntimeFileResult result = m_runtimeFileSystem->writeTextFile(textFile->StorageId, m_text);
            if (!result.success) {
                std::cerr << "[CodeEditorPanel] Failed to write TextFile " << textFile->Path
                          << ": " << result.error << '\n';
                return false;
            }
        } else {
            std::ofstream output(AssetPath::fromStored(textFile->Path), std::ios::binary | std::ios::trunc);
            if (!output.is_open()) {
                std::cerr << "[CodeEditorPanel] Failed to write TextFile: " << textFile->Path << '\n';
                return false;
            }
            output.write(m_text.data(), static_cast<std::streamsize>(m_text.size()));
            if (!output.good()) return false;
        }
    } else {
        return false;
    }
    m_loadedText = m_text;
    m_dirty = false;
    return true;
}

void CodeEditorPanel::drawCloseConfirmation() {
    if (!m_confirmClose) return;
    ImGui::OpenPopup("###CodeEditorUnsavedChanges");
    const std::string popupTitle = std::string(Loc::t(Loc::LocKey::UnsavedCodeTitle)) +
        "###CodeEditorUnsavedChanges";
    if (ImGui::BeginPopupModal(popupTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", Loc::t(Loc::LocKey::UnsavedCodeLine1));
        ImGui::Text("%s", Loc::t(Loc::LocKey::UnsavedCodeCloseLine));
        ImGui::Separator();
        if (EditorUi::dangerButton(Loc::t(Loc::LocKey::SaveButton),
                                   m_confirmCloseOpenedAt, 0.0f)) {
            if (save()) {
                m_confirmClose = false;
                m_closeRequested = true;
                isOpen = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (EditorUi::dangerButton(Loc::t(Loc::LocKey::CrashRecoveryDiscard),
                                   m_confirmCloseOpenedAt, 0.0f)) {
            m_confirmClose = false; m_closeRequested = true; isOpen = false; ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (EditorUi::safeButton(Loc::t(Loc::LocKey::Cancel))) {
            m_confirmClose = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void CodeEditorPanel::onRender() {
    auto instance = m_target.lock();
    if (!instance) { isOpen = false; m_closeRequested = true; return; }
    if (!m_loaded) loadTarget();
    const std::string windowTitle = instance->Name + (m_dirty ? "*" : "") +
        "###CodeEditor_" + std::to_string(reinterpret_cast<uintptr_t>(instance.get()));
    ImGui::SetNextWindowDockID(m_dockspaceId, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(720.0f, 460.0f), ImGuiCond_FirstUseEver);
    const bool wasOpen = isOpen;
    if (!ImGui::Begin(windowTitle.c_str(), &isOpen)) {
        m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        ImGui::End();
    } else {
        m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::GetIO().KeyCtrl) {
            const bool increase = ImGui::IsKeyPressed(ImGuiKey_Equal) ||
                ImGui::IsKeyPressed(ImGuiKey_KeypadAdd) ||
                (ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Semicolon));
            const bool decrease = ImGui::IsKeyPressed(ImGuiKey_Minus) ||
                ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract);
            if (increase && !decrease) {
                m_codeFontSize = std::min(m_codeFontSize + CODE_EDITOR_FONT_SIZE_STEP,
                                          CODE_EDITOR_MAX_FONT_SIZE);
            } else if (decrease && !increase) {
                m_codeFontSize = std::max(m_codeFontSize - CODE_EDITOR_FONT_SIZE_STEP,
                                          CODE_EDITOR_MIN_FONT_SIZE);
            }
        }

        ImGui::PushFont(m_codeFont, m_codeFontSize);
        const std::string lastLineNumber =
            std::to_string(std::max(1, static_cast<int>(m_lineStarts.size())));
        const float gutterWidth = std::max(
            CODE_EDITOR_GUTTER_WIDTH, ImGui::CalcTextSize(lastLineNumber.c_str()).x + 16.0f);
        ImGuiWindow* parent = GImGui->CurrentWindow;
        const ImGuiID inputId = parent->GetID("##CodeInput");
        const std::string before = m_text;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(gutterWidth + CODE_EDITOR_FRAME_LEFT_PADDING,
                                   CODE_EDITOR_FRAME_TOP_PADDING));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.025f, 0.040f, 0.075f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_InputTextCursor, ImVec4(0.45f, 0.85f, 1.0f, 1.0f));
        ImGui::InputTextMultiline("##CodeInput", m_text.data(), m_text.size() + 1,
            ImVec2(-FLT_MIN, -FLT_MIN), ImGuiInputTextFlags_AllowTabInput |
            ImGuiInputTextFlags_CallbackResize, &CodeEditorPanel::resizeInputCallback, &m_text);
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        if (m_text != before) {
            m_text.resize(std::strlen(m_text.c_str()));
            m_dirty = m_text != m_loadedText;
            rebuildHighlightCache();
        }
        drawHighlightedText(parent, inputId, gutterWidth);
        ImGui::PopFont();
        ImGui::End();
    }
    if (wasOpen && !isOpen) {
        if (m_dirty) {
            isOpen = true;
            if (!m_confirmClose) m_confirmCloseOpenedAt = ImGui::GetTime();
            m_confirmClose = true;
        }
        else m_closeRequested = true;
    }
    drawCloseConfirmation();
}
