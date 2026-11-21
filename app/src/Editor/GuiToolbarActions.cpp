#include "include/Editor/GuiToolbarActions.hpp"
#include "include/Editor/CommandHistory.hpp"
#include "include/Editor/SceneHierarchyPanel.hpp"
#include "include/Core/PropertyRegistry.hpp"
#include "include/Core/SceneLoader.hpp"
#include "include/Core/ScreenGuiLayout.hpp"
#include "include/Instances/ScreenGuiObject.hpp"
#include "include/Instances/System.hpp"
#include "include/Instances/Workspace.hpp"
#include "include/Util/Logger.hpp"

#include <algorithm>

namespace GuiToolbarActions {

namespace {

const PropertyDesc* findProperty(std::string_view className, std::string_view name) {
    for (const auto& desc : PropertyRegistry::schemaFor(className)) {
        if (desc.name == name) return &desc;
    }
    RCBN_ERROR(std::string("GuiToolbarActions: property is not registered: ") +
               std::string(className) + "." + std::string(name));
    return nullptr;
}

// 1つの変更をまとめた Undo 単位へ積む（値が同じなら積まない）。扱う値は Vector2 のみ
void addChange(CompositeCommand& composite, Instance& target, const PropertyDesc& desc,
               const Vector2& afterValue) {
    const PropValue before = desc.get(&target);
    const auto* beforeValue = std::get_if<Vector2>(&before);
    if (beforeValue && *beforeValue == afterValue) return;
    const PropValue after(afterValue);
    composite.add(std::make_unique<SetPropertyCommand>(target.shared_from_this(), &desc, before, after));
}

// 変更があれば Undo 1回分として適用する
bool commit(std::unique_ptr<CompositeCommand> composite, CommandHistory* history) {
    if (composite->empty()) return false;
    if (history) history->execute(std::move(composite));
    else composite->execute();
    return true;
}

// 基準解像度空間（ビューポート原点0、ピクセル倍率1）での矩形
ScreenGuiLayout::Rect baseRect(const ScreenGuiObject& object, const Vector2& base) {
    return ScreenGuiLayout::computeRect(object, 0.0f, 0.0f, base.x, base.y, Vector2(1.0f, 1.0f));
}

// 基準解像度空間の左上座標を、要素の単位系(Pixel/Scale)の Position へ戻す
Vector2 positionFromBase(const ScreenGuiObject& object, float x, float y, const Vector2& base) {
    if (object.NormType == Norm::Scale) return Vector2(x / base.x, y / base.y);
    return Vector2(x, y);
}

} // namespace

const std::vector<ResolutionPreset>& resolutionPresets() {
    static const std::vector<ResolutionPreset> presets = {
        {"16:9  1280 x 720", Vector2(1280.0f, 720.0f)},
        {"16:9  1600 x 900", Vector2(1600.0f, 900.0f)},
        {"16:9  1920 x 1080", Vector2(1920.0f, 1080.0f)},
        {"16:9  2560 x 1440", Vector2(2560.0f, 1440.0f)},
        {"16:9  3840 x 2160", Vector2(3840.0f, 2160.0f)},
        {"4:3  800 x 600", Vector2(800.0f, 600.0f)},
        {"4:3  1024 x 768", Vector2(1024.0f, 768.0f)},
        {"4:3  1280 x 960", Vector2(1280.0f, 960.0f)},
        {"4:3  1600 x 1200", Vector2(1600.0f, 1200.0f)},
    };
    return presets;
}

Vector2 baseResolution(const Workspace& workspace) {
    Vector2 base(1920.0f, 1080.0f);
    if (auto parent = workspace.Parent.lock(); parent && parent->IsA("System")) {
        const auto* system = static_cast<const System*>(parent.get());
        if (system->BaseResolution.x > 0.0f) base.x = system->BaseResolution.x;
        if (system->BaseResolution.y > 0.0f) base.y = system->BaseResolution.y;
    }
    return base;
}

std::vector<ScreenGuiObject*> editableSelection(const std::vector<Instance*>& selection,
                                                Instance* primary) {
    std::vector<ScreenGuiObject*> result;
    auto addIfEditable = [&result](Instance* instance) {
        if (!instance || !instance->IsA("ScreenGuiObject")) return;
        auto* gui = static_cast<ScreenGuiObject*>(instance);
        if (gui->Locked || ScreenGuiLayout::hasWorldGuiAncestor(*gui)) return;
        if (std::find(result.begin(), result.end(), gui) == result.end()) result.push_back(gui);
    };
    if (!selection.empty()) {
        for (Instance* instance : selection) addIfEditable(instance);
    } else {
        addIfEditable(primary);
    }
    return result;
}

bool align(const std::vector<ScreenGuiObject*>& targets, const Workspace& workspace,
           Align alignTo, CommandHistory* history) {
    const PropertyDesc* positionDesc = findProperty("ScreenGuiObject", "Position");
    if (!positionDesc || targets.empty()) return false;

    const Vector2 base = baseResolution(workspace);
    // 1つなら画面、複数なら選択全体のAABBを基準にする
    ScreenGuiLayout::Rect bounds{0.0f, 0.0f, base.x, base.y};
    if (targets.size() > 1) {
        bounds = baseRect(*targets.front(), base);
        for (const ScreenGuiObject* target : targets) {
            const ScreenGuiLayout::Rect rect = baseRect(*target, base);
            const float right = (std::max)(bounds.x + bounds.w, rect.x + rect.w);
            const float bottom = (std::max)(bounds.y + bounds.h, rect.y + rect.h);
            bounds.x = (std::min)(bounds.x, rect.x);
            bounds.y = (std::min)(bounds.y, rect.y);
            bounds.w = right - bounds.x;
            bounds.h = bottom - bounds.y;
        }
    }

    auto composite = std::make_unique<CompositeCommand>();
    for (ScreenGuiObject* target : targets) {
        const ScreenGuiLayout::Rect rect = baseRect(*target, base);
        float x = rect.x;
        float y = rect.y;
        switch (alignTo) {
            case Align::Left:    x = bounds.x; break;
            case Align::CenterX: x = bounds.x + (bounds.w - rect.w) * 0.5f; break;
            case Align::Right:   x = bounds.x + bounds.w - rect.w; break;
            case Align::Top:     y = bounds.y; break;
            case Align::CenterY: y = bounds.y + (bounds.h - rect.h) * 0.5f; break;
            case Align::Bottom:  y = bounds.y + bounds.h - rect.h; break;
        }
        addChange(*composite, *target, *positionDesc, positionFromBase(*target, x, y, base));
    }
    return commit(std::move(composite), history);
}

bool fitToScreen(const std::vector<ScreenGuiObject*>& targets, const Workspace& workspace,
                 CommandHistory* history) {
    const PropertyDesc* positionDesc = findProperty("ScreenGuiObject", "Position");
    const PropertyDesc* sizeDesc = findProperty("GuiObject", "Size");
    if (!positionDesc || !sizeDesc || targets.empty()) return false;

    const Vector2 base = baseResolution(workspace);
    auto composite = std::make_unique<CompositeCommand>();
    for (ScreenGuiObject* target : targets) {
        const Vector2 size = target->NormType == Norm::Scale ? Vector2(1.0f, 1.0f) : base;
        addChange(*composite, *target, *positionDesc, Vector2(0.0f, 0.0f));
        addChange(*composite, *target, *sizeDesc, size);
    }
    return commit(std::move(composite), history);
}

std::shared_ptr<ScreenGuiObject> addElement(const std::string& className, Workspace& workspace,
                                            CommandHistory* history) {
    std::shared_ptr<Instance> instance = SceneLoader::createInstance(className);
    if (!instance || !instance->IsA("ScreenGuiObject")) {
        RCBN_ERROR("GuiToolbarActions: cannot create screen GUI class: " + className);
        return nullptr;
    }
    auto gui = std::static_pointer_cast<ScreenGuiObject>(instance);
    // 文字を持つ要素は中身が見えるようにクラス名を入れておく
    if (TextContent* text = gui->textContent()) text->Text = className;

    const Vector2 base = baseResolution(workspace);
    gui->Position = Vector2((base.x - gui->Size.x) * 0.5f, (base.y - gui->Size.y) * 0.5f);

    auto parent = workspace.shared_from_this();
    gui->Name = SceneHierarchyPanel::uniqueName(parent, className);
    auto command = std::make_unique<AddInstanceCommand>(parent, gui);
    if (history) history->execute(std::move(command));
    else command->execute();
    return gui;
}

bool setBaseResolution(System& system, const Vector2& resolution, CommandHistory* history) {
    const PropertyDesc* desc = findProperty("System", "BaseResolution");
    if (!desc) return false;
    auto composite = std::make_unique<CompositeCommand>();
    addChange(*composite, system, *desc, resolution);
    return commit(std::move(composite), history);
}

} // namespace GuiToolbarActions
