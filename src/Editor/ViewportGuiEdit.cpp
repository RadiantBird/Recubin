// ===================================================
//  ViewportPanel — 画面GUI(ScreenGuiObject)のビューポート編集
//  クリック選択・ドラッグ移動・Shiftでのリサイズ・選択枠・吸着ガイド
//  （ツールバーのGUIタブ中のみ。Play中は何もしない）
// ===================================================
#include <Editor/ViewportPanel.hpp>
#include <Editor/CommandHistory.hpp>
#include <Editor/GuiToolbarActions.hpp>
#include <Core/PropertyRegistry.hpp>
#include <Core/SystemState.hpp>
#include <Instances/ScreenGuiObject.hpp>
#include <Util/Logger.hpp>
#include <include/imgui/imgui.h>
#include <algorithm>
#include <cmath>

namespace {

constexpr float GUI_DRAG_THRESHOLD_SQ = 25.0f;  // 5px（3Dのフリードラッグと同じ閾値）
constexpr float GUI_SNAP_DISTANCE = 6.0f;       // 吸着距離（画面座標）
constexpr float GUI_HANDLE_HALF = 4.0f;         // リサイズハンドルの半径（描画）
constexpr float GUI_HANDLE_HIT_HALF = 7.0f;     // リサイズハンドルの当たり判定の半径
constexpr float GUI_MIN_RESIZE = 1.0f;          // リサイズ時の最小の幅・高さ（画面座標）

constexpr ImU32 GUI_SELECTION_COLOR = IM_COL32(0, 170, 255, 255);
constexpr ImU32 GUI_SECONDARY_SELECTION_COLOR = IM_COL32(0, 170, 255, 150);
constexpr ImU32 GUI_HOVER_COLOR = IM_COL32(255, 255, 255, 110);
constexpr ImU32 GUI_GUIDE_COLOR = IM_COL32(255, 70, 200, 230);
constexpr ImU32 GUI_HANDLE_FILL_COLOR = IM_COL32(255, 255, 255, 255);

const PropertyDesc* findGuiProperty(std::string_view className, std::string_view name) {
    for (const auto& desc : PropertyRegistry::schemaFor(className)) {
        if (desc.name == name) return &desc;
    }
    RCBN_ERROR("ViewportGuiEdit: property is not registered: " + std::string(className) + "." +
               std::string(name));
    return nullptr;
}

const PropertyDesc* screenGuiPositionProperty() { return findGuiProperty("ScreenGuiObject", "Position"); }
const PropertyDesc* guiSizeProperty() { return findGuiProperty("GuiObject", "Size"); }

ScreenGuiLayout::Rect viewportRect(const ImVec2& origin, int width, int height) {
    return {origin.x, origin.y, static_cast<float>(width), static_cast<float>(height)};
}

ScreenGuiLayout::Rect unionRect(const ScreenGuiLayout::Rect& a, const ScreenGuiLayout::Rect& b) {
    const float left = (std::min)(a.x, b.x);
    const float top = (std::min)(a.y, b.y);
    const float right = (std::max)(a.x + a.w, b.x + b.w);
    const float bottom = (std::max)(a.y + a.h, b.y + b.h);
    return {left, top, right - left, bottom - top};
}

void drawRectOutline(ImDrawList* drawList, const ScreenGuiLayout::Rect& rect, ImU32 color, float thickness) {
    drawList->AddRect(ImVec2(rect.x, rect.y), ImVec2(rect.x + rect.w, rect.y + rect.h),
                      color, 0.0f, 0, thickness);
}

// 画面座標の長さを、要素の単位系(Pixel/Scale)の長さへ戻すための1単位あたりの画面長
Vector2 guiUnit(const ScreenGuiObject& gui, const ScreenGuiLayout::Rect& viewport, const Vector2& scale) {
    return gui.NormType == Norm::Scale ? Vector2(viewport.w, viewport.h) : scale;
}

ImGuiMouseCursor resizeCursor(const GuiSnap::ResizeHandle& handle) {
    if (handle.x == 0) return ImGuiMouseCursor_ResizeNS;
    if (handle.y == 0) return ImGuiMouseCursor_ResizeEW;
    return handle.x == handle.y ? ImGuiMouseCursor_ResizeNWSE : ImGuiMouseCursor_ResizeNESW;
}

} // namespace

bool ViewportPanel::isGuiEditMode() const {
    return m_guiEdit && m_guiEdit->active && workspace && selectedInstance &&
           !SystemState::get().isPlaying;
}

void ViewportPanel::updateGuiEdit(const ViewportLayout& layout) {
    if (!workspace) return;
    if (!isGuiEditMode()) {
        // GUIタブ以外ではGUIはクリックを素通りする（選択中GUIの枠だけ表示する）
        if (m_guiDragging) endGuiDrag();
        if (m_guiResizing) endGuiResize();
        m_guiDragArmed = false;
        if (!SystemState::get().isPlaying) drawGuiOverlay(layout, nullptr);
        return;
    }

    const ImVec2 mouse = ImGui::GetMousePos();
    // TextButton の InvisibleButton が押下でアクティブになってもGUI上の操作を受け付ける
    const bool hovering = ImGui::IsWindowHovered(
        ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool busy = m_guiDragging || m_guiResizing;
    ScreenGuiObject* hovered = (hovering && !busy) ? findGuiAt(layout, mouse) : nullptr;

    // Shift 押下中だけリサイズハンドルを出し、ハンドル上の操作を要素の選択より優先する
    GuiSnap::ResizeHandle hoveredHandle;
    const bool onHandle = hovering && !busy && ImGui::GetIO().KeyShift &&
                          findGuiResizeHandleAt(layout, mouse, hoveredHandle);
    if (onHandle) ImGui::SetMouseCursor(resizeCursor(hoveredHandle));

    if (hovering && ImGui::IsMouseClicked(0) && !busy) {
        const ScreenGuiLayout::Rect viewport = viewportRect(layout.contentOrigin, layout.width, layout.height);
        if (onHandle) {
            beginGuiResize(layout, hoveredHandle);
        } else if (hovered) {
            selectGuiFromClick(hovered);
            const std::vector<ScreenGuiObject*> selected = selectedScreenGuiObjects();
            const bool hitSelected = std::find(selected.begin(), selected.end(), hovered) != selected.end();
            m_guiDragArmed = hitSelected && !ImGui::GetIO().KeyCtrl;
            m_guiDragStart = mouse;
        } else if (viewport.contains(mouse.x, mouse.y) && !ImGui::GetIO().KeyCtrl) {
            // GUIの無い場所（またはLockedのGUIだけ）のクリックは選択解除
            *selectedInstance = nullptr;
            if (selectedInstances) selectedInstances->clear();
        }
    }

    if (m_guiResizing) {
        ImGui::SetMouseCursor(resizeCursor(m_guiResizeHandle));
        if (ImGui::IsMouseDown(0)) updateGuiResize(layout);
        else endGuiResize();
    }

    if (m_guiDragArmed && !m_guiDragging && ImGui::IsMouseDown(0)) {
        const float dx = mouse.x - m_guiDragStart.x;
        const float dy = mouse.y - m_guiDragStart.y;
        if (dx * dx + dy * dy > GUI_DRAG_THRESHOLD_SQ) beginGuiDrag(layout);
    }

    if (m_guiDragging) {
        if (ImGui::IsMouseDown(0)) updateGuiDrag(layout);
        else endGuiDrag();
    } else if (!ImGui::IsMouseDown(0)) {
        m_guiDragArmed = false;
    }

    drawGuiOverlay(layout, hovered);
}

ScreenGuiObject* ViewportPanel::findGuiAt(const ViewportLayout& layout, const ImVec2& point) const {
    const ScreenGuiLayout::Rect viewport = viewportRect(layout.contentOrigin, layout.width, layout.height);
    if (!viewport.contains(point.x, point.y)) return nullptr;

    const std::vector<ScreenGuiObject*> elements = ScreenGuiLayout::collectScreenElements(*workspace);
    const Vector2 scale = ScreenGuiLayout::pixelScale(*workspace, viewport.w, viewport.h, layout.uniformGuiScale);
    // ZIndex 昇順で描画されるので、後ろから見て最初に当たったものが最前面
    for (auto it = elements.rbegin(); it != elements.rend(); ++it) {
        ScreenGuiObject* element = *it;
        // Locked はクリックを素通りし、背後の要素を選べるようにする
        if (!element->Visible || element->Locked) continue;
        const ScreenGuiLayout::Rect rect = ScreenGuiLayout::computeRect(
            *element, viewport.x, viewport.y, viewport.w, viewport.h, scale);
        if (rect.contains(point.x, point.y)) return element;
    }
    return nullptr;
}

std::vector<ScreenGuiObject*> ViewportPanel::selectedScreenGuiObjects() const {
    std::vector<ScreenGuiObject*> result;
    auto addIfScreenGui = [&result](Instance* instance) {
        if (!instance || !instance->IsA("ScreenGuiObject")) return;
        auto* gui = static_cast<ScreenGuiObject*>(instance);
        if (ScreenGuiLayout::hasWorldGuiAncestor(*gui)) return;
        if (std::find(result.begin(), result.end(), gui) == result.end()) result.push_back(gui);
    };
    if (selectedInstances && !selectedInstances->empty()) {
        for (Instance* instance : *selectedInstances) addIfScreenGui(instance);
    } else if (selectedInstance) {
        addIfScreenGui(*selectedInstance);
    }
    return result;
}

void ViewportPanel::selectGuiFromClick(ScreenGuiObject* hit) {
    if (!hit || !selectedInstance) return;
    Instance* instance = hit;
    if (ImGui::GetIO().KeyCtrl && selectedInstances) {
        // Ctrl+クリック: 3Dと同じく選択へ追加・解除する
        auto it = std::find(selectedInstances->begin(), selectedInstances->end(), instance);
        if (it != selectedInstances->end()) {
            selectedInstances->erase(it);
            if (*selectedInstance == instance)
                *selectedInstance = selectedInstances->empty() ? nullptr : selectedInstances->back();
        } else {
            selectedInstances->push_back(instance);
            *selectedInstance = instance;
        }
        return;
    }
    const bool alreadySelected = selectedInstances &&
        std::find(selectedInstances->begin(), selectedInstances->end(), instance) != selectedInstances->end();
    *selectedInstance = instance;
    // 選択済み要素からのドラッグは複数選択を維持する
    if (selectedInstances && !alreadySelected) {
        selectedInstances->clear();
        selectedInstances->push_back(instance);
    }
}

std::vector<ScreenGuiLayout::Rect> ViewportPanel::guiSnapTargets(const ViewportLayout& layout,
                                                                 const Vector2& scale) const {
    // 吸着先: ビューポート自身と、選択外の表示中GUI
    const ScreenGuiLayout::Rect viewport = viewportRect(layout.contentOrigin, layout.width, layout.height);
    std::vector<ScreenGuiLayout::Rect> targets{viewport};
    const std::vector<ScreenGuiObject*> selected = selectedScreenGuiObjects();
    for (ScreenGuiObject* element : ScreenGuiLayout::collectScreenElements(*workspace)) {
        if (!element->Visible) continue;
        if (std::find(selected.begin(), selected.end(), element) != selected.end()) continue;
        targets.push_back(ScreenGuiLayout::computeRect(
            *element, viewport.x, viewport.y, viewport.w, viewport.h, scale));
    }
    return targets;
}

void ViewportPanel::beginGuiDrag(const ViewportLayout& layout) {
    // Explorer から選んだ Locked の要素はドラッグで動かさない
    const std::vector<ScreenGuiObject*> selected = GuiToolbarActions::editableSelection(
        selectedInstances ? *selectedInstances : std::vector<Instance*>{}, *selectedInstance);
    m_guiDragArmed = false;
    if (selected.empty()) return;

    const ScreenGuiLayout::Rect viewport = viewportRect(layout.contentOrigin, layout.width, layout.height);
    const Vector2 scale = ScreenGuiLayout::pixelScale(*workspace, viewport.w, viewport.h, layout.uniformGuiScale);
    m_guiDragEntries.clear();
    bool hasBounds = false;
    for (ScreenGuiObject* gui : selected) {
        m_guiDragEntries.push_back({gui->weak_from_this(), gui->Position});
        const ScreenGuiLayout::Rect rect = ScreenGuiLayout::computeRect(
            *gui, viewport.x, viewport.y, viewport.w, viewport.h, scale);
        m_guiDragStartBounds = hasBounds ? unionRect(m_guiDragStartBounds, rect) : rect;
        hasBounds = true;
    }
    m_guiSnap = {};
    m_guiDragging = true;
}

void ViewportPanel::updateGuiDrag(const ViewportLayout& layout) {
    const PropertyDesc* positionDesc = screenGuiPositionProperty();
    if (!positionDesc) {
        endGuiDrag();
        return;
    }

    const ScreenGuiLayout::Rect viewport = viewportRect(layout.contentOrigin, layout.width, layout.height);
    const Vector2 scale = ScreenGuiLayout::pixelScale(*workspace, viewport.w, viewport.h, layout.uniformGuiScale);
    const ImVec2 mouse = ImGui::GetMousePos();
    float dx = mouse.x - m_guiDragStart.x;
    float dy = mouse.y - m_guiDragStart.y;

    // 吸着はツールバーのON/OFFに従い、Alt 押下中は一時的に無効にする
    m_guiSnap = {};
    const bool snapEnabled = !m_guiEdit || m_guiEdit->snap;
    if (snapEnabled && !ImGui::GetIO().KeyAlt) {
        ScreenGuiLayout::Rect moved = m_guiDragStartBounds;
        moved.x += dx;
        moved.y += dy;
        m_guiSnap = GuiSnap::snapRect(moved, guiSnapTargets(layout, scale), GUI_SNAP_DISTANCE);
        dx += m_guiSnap.x.delta;
        dy += m_guiSnap.y.delta;
    }

    for (const GuiDragEntry& entry : m_guiDragEntries) {
        auto instance = entry.target.lock();
        if (!instance) continue;
        auto* gui = static_cast<ScreenGuiObject*>(instance.get());
        const Vector2 unit = guiUnit(*gui, viewport, scale);
        if (unit.x <= 0.0f || unit.y <= 0.0f) continue;
        const Vector2 position(entry.beforePosition.x + dx / unit.x,
                               entry.beforePosition.y + dy / unit.y);
        PropertyRegistry::writeValue(gui, *positionDesc, PropValue(position));
    }
}

void ViewportPanel::endGuiDrag() {
    const PropertyDesc* positionDesc = screenGuiPositionProperty();
    if (positionDesc && m_history) {
        // ドラッグ全体を Undo 1回分として記録する（値は適用済みなので record）
        auto composite = std::make_unique<CompositeCommand>();
        for (const GuiDragEntry& entry : m_guiDragEntries) {
            auto instance = entry.target.lock();
            if (!instance) continue;
            const Vector2 after = static_cast<ScreenGuiObject*>(instance.get())->Position;
            if (after == entry.beforePosition) continue;
            composite->add(std::make_unique<SetPropertyCommand>(
                instance, positionDesc, PropValue(entry.beforePosition), PropValue(after)));
        }
        if (!composite->empty()) m_history->record(std::move(composite));
    }
    m_guiDragEntries.clear();
    m_guiSnap = {};
    m_guiDragging = false;
    m_guiDragArmed = false;
}

ScreenGuiObject* ViewportPanel::guiResizeTarget() const {
    // リサイズはプライマリ選択の1要素だけ（Locked・WorldGui配下は対象外）
    if (!selectedInstance || !*selectedInstance) return nullptr;
    const std::vector<ScreenGuiObject*> editable =
        GuiToolbarActions::editableSelection({}, *selectedInstance);
    return editable.empty() ? nullptr : editable.front();
}

bool ViewportPanel::findGuiResizeHandleAt(const ViewportLayout& layout, const ImVec2& point,
                                          GuiSnap::ResizeHandle& handle) const {
    ScreenGuiObject* target = guiResizeTarget();
    if (!target || !target->Visible) return false;
    const ScreenGuiLayout::Rect viewport = viewportRect(layout.contentOrigin, layout.width, layout.height);
    const Vector2 scale = ScreenGuiLayout::pixelScale(*workspace, viewport.w, viewport.h, layout.uniformGuiScale);
    const ScreenGuiLayout::Rect rect = ScreenGuiLayout::computeRect(
        *target, viewport.x, viewport.y, viewport.w, viewport.h, scale);
    for (const GuiSnap::ResizeHandle& candidate : GuiSnap::resizeHandles()) {
        const float cx = GuiSnap::handleCenterX(rect, candidate);
        const float cy = GuiSnap::handleCenterY(rect, candidate);
        if (std::fabs(point.x - cx) <= GUI_HANDLE_HIT_HALF && std::fabs(point.y - cy) <= GUI_HANDLE_HIT_HALF) {
            handle = candidate;
            return true;
        }
    }
    return false;
}

void ViewportPanel::beginGuiResize(const ViewportLayout& layout, const GuiSnap::ResizeHandle& handle) {
    ScreenGuiObject* target = guiResizeTarget();
    if (!target) return;
    const ScreenGuiLayout::Rect viewport = viewportRect(layout.contentOrigin, layout.width, layout.height);
    const Vector2 scale = ScreenGuiLayout::pixelScale(*workspace, viewport.w, viewport.h, layout.uniformGuiScale);
    m_guiResizeTarget = target->weak_from_this();
    m_guiResizeHandle = handle;
    m_guiResizeStartRect = ScreenGuiLayout::computeRect(
        *target, viewport.x, viewport.y, viewport.w, viewport.h, scale);
    m_guiResizeBeforePosition = target->Position;
    m_guiResizeBeforeSize = target->Size;
    m_guiDragStart = ImGui::GetMousePos();
    m_guiDragArmed = false;
    m_guiSnap = {};
    m_guiResizing = true;
}

void ViewportPanel::updateGuiResize(const ViewportLayout& layout) {
    const PropertyDesc* positionDesc = screenGuiPositionProperty();
    const PropertyDesc* sizeDesc = guiSizeProperty();
    auto instance = m_guiResizeTarget.lock();
    if (!positionDesc || !sizeDesc || !instance) {
        endGuiResize();
        return;
    }
    auto* gui = static_cast<ScreenGuiObject*>(instance.get());
    const ScreenGuiLayout::Rect viewport = viewportRect(layout.contentOrigin, layout.width, layout.height);
    const Vector2 scale = ScreenGuiLayout::pixelScale(*workspace, viewport.w, viewport.h, layout.uniformGuiScale);
    const Vector2 unit = guiUnit(*gui, viewport, scale);
    if (unit.x <= 0.0f || unit.y <= 0.0f) return;

    const ImVec2 mouse = ImGui::GetMousePos();
    // 吸着はドラッグ移動と同じくツールバーのON/OFFとAltに従う
    const bool snapEnabled = (!m_guiEdit || m_guiEdit->snap) && !ImGui::GetIO().KeyAlt;
    const ScreenGuiLayout::Rect rect = GuiSnap::resizeRect(
        m_guiResizeStartRect, m_guiResizeHandle,
        mouse.x - m_guiDragStart.x, mouse.y - m_guiDragStart.y,
        snapEnabled ? guiSnapTargets(layout, scale) : std::vector<ScreenGuiLayout::Rect>{},
        snapEnabled ? GUI_SNAP_DISTANCE : 0.0f, GUI_MIN_RESIZE, m_guiSnap);

    const Vector2 position((rect.x - viewport.x) / unit.x, (rect.y - viewport.y) / unit.y);
    const Vector2 size(rect.w / unit.x, rect.h / unit.y);
    PropertyRegistry::writeValue(gui, *positionDesc, PropValue(position));
    PropertyRegistry::writeValue(gui, *sizeDesc, PropValue(size));
}

void ViewportPanel::endGuiResize() {
    const PropertyDesc* positionDesc = screenGuiPositionProperty();
    const PropertyDesc* sizeDesc = guiSizeProperty();
    auto instance = m_guiResizeTarget.lock();
    if (positionDesc && sizeDesc && instance && m_history) {
        // リサイズ全体（Position と Size）を Undo 1回分として記録する
        const auto* gui = static_cast<ScreenGuiObject*>(instance.get());
        auto composite = std::make_unique<CompositeCommand>();
        if (!(gui->Position == m_guiResizeBeforePosition))
            composite->add(std::make_unique<SetPropertyCommand>(
                instance, positionDesc, PropValue(m_guiResizeBeforePosition), PropValue(gui->Position)));
        if (!(gui->Size == m_guiResizeBeforeSize))
            composite->add(std::make_unique<SetPropertyCommand>(
                instance, sizeDesc, PropValue(m_guiResizeBeforeSize), PropValue(gui->Size)));
        if (!composite->empty()) m_history->record(std::move(composite));
    }
    m_guiResizeTarget.reset();
    m_guiSnap = {};
    m_guiResizing = false;
}

void ViewportPanel::drawGuiOverlay(const ViewportLayout& layout, ScreenGuiObject* hovered) const {
    const ScreenGuiLayout::Rect viewport = viewportRect(layout.contentOrigin, layout.width, layout.height);
    const Vector2 scale = ScreenGuiLayout::pixelScale(*workspace, viewport.w, viewport.h, layout.uniformGuiScale);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(ImVec2(viewport.x, viewport.y),
                           ImVec2(viewport.x + viewport.w, viewport.y + viewport.h), true);

    const std::vector<ScreenGuiObject*> selected = selectedScreenGuiObjects();
    if (hovered && std::find(selected.begin(), selected.end(), hovered) == selected.end()) {
        drawRectOutline(drawList, ScreenGuiLayout::computeRect(
            *hovered, viewport.x, viewport.y, viewport.w, viewport.h, scale), GUI_HOVER_COLOR, 1.0f);
    }
    // 選択枠は要素の矩形ぴったりに描く（プライマリを強調）
    const Instance* primary = selectedInstance ? *selectedInstance : nullptr;
    for (ScreenGuiObject* gui : selected) {
        const bool isPrimary = gui == primary;
        drawRectOutline(drawList, ScreenGuiLayout::computeRect(
            *gui, viewport.x, viewport.y, viewport.w, viewport.h, scale),
            isPrimary ? GUI_SELECTION_COLOR : GUI_SECONDARY_SELECTION_COLOR, isPrimary ? 2.0f : 1.5f);
    }

    // リサイズハンドルは GUIタブで Shift 押下中（またはリサイズ中）だけ出す。普段は精密な移動の邪魔をしない
    const bool showHandles = isGuiEditMode() && (m_guiResizing || ImGui::GetIO().KeyShift);
    if (ScreenGuiObject* target = showHandles ? guiResizeTarget() : nullptr; target && target->Visible) {
        const ScreenGuiLayout::Rect rect = ScreenGuiLayout::computeRect(
            *target, viewport.x, viewport.y, viewport.w, viewport.h, scale);
        for (const GuiSnap::ResizeHandle& handle : GuiSnap::resizeHandles()) {
            const ImVec2 center(GuiSnap::handleCenterX(rect, handle), GuiSnap::handleCenterY(rect, handle));
            const ImVec2 min(center.x - GUI_HANDLE_HALF, center.y - GUI_HANDLE_HALF);
            const ImVec2 max(center.x + GUI_HANDLE_HALF, center.y + GUI_HANDLE_HALF);
            drawList->AddRectFilled(min, max, GUI_HANDLE_FILL_COLOR);
            drawList->AddRect(min, max, GUI_SELECTION_COLOR, 0.0f, 0, 1.5f);
        }
    }

    const bool showGuides = m_guiDragging || m_guiResizing;
    if (showGuides && m_guiSnap.x.snapped) {
        drawList->AddLine(ImVec2(m_guiSnap.x.guide, viewport.y),
                          ImVec2(m_guiSnap.x.guide, viewport.y + viewport.h), GUI_GUIDE_COLOR, 1.0f);
    }
    if (showGuides && m_guiSnap.y.snapped) {
        drawList->AddLine(ImVec2(viewport.x, m_guiSnap.y.guide),
                          ImVec2(viewport.x + viewport.w, m_guiSnap.y.guide), GUI_GUIDE_COLOR, 1.0f);
    }
    drawList->PopClipRect();
}
