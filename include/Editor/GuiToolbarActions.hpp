#pragma once
#include <Math/Vector2.hpp>
#include <memory>
#include <string>
#include <vector>

class CommandHistory;
class Instance;
class ScreenGuiObject;
class System;
class Workspace;

// ツールバーのGUIタブから行う画面GUIの一括操作（追加・揃え・全画面化・基準解像度）。
// 座標は System.BaseResolution の論理ピクセル空間で計算し、変更は CommandHistory 経由で
// Undo 1回分として適用する（history が null なら直接適用する）。
namespace GuiToolbarActions {

enum class Align { Left, CenterX, Right, Top, CenterY, Bottom };

struct ResolutionPreset {
    const char* label;
    Vector2 size;
};

// 基準解像度のプリセット（16:9 と 4:3）
const std::vector<ResolutionPreset>& resolutionPresets();

// System.BaseResolution（System が無い/不正なら 1920x1080）
Vector2 baseResolution(const Workspace& workspace);

// 選択から操作対象の画面GUIを取り出す（WorldGui配下・Locked は除く）
std::vector<ScreenGuiObject*> editableSelection(const std::vector<Instance*>& selection,
                                                Instance* primary);

// 1つなら画面に、複数なら選択全体のAABBに揃える。値が変わったら true
bool align(const std::vector<ScreenGuiObject*>& targets, const Workspace& workspace,
           Align align, CommandHistory* history);

// Position を原点、Size を画面いっぱいにする。値が変わったら true
bool fitToScreen(const std::vector<ScreenGuiObject*>& targets, const Workspace& workspace,
                 CommandHistory* history);

// className の画面GUIを Workspace 直下の画面中央へ追加する。失敗時は nullptr
std::shared_ptr<ScreenGuiObject> addElement(const std::string& className, Workspace& workspace,
                                            CommandHistory* history);

// System.BaseResolution を変更する。値が変わったら true
bool setBaseResolution(System& system, const Vector2& resolution, CommandHistory* history);

} // namespace GuiToolbarActions
