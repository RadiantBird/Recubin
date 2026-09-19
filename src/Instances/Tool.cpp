#include <Instances/Tool.hpp>
#include <Core/User.hpp>
#include <Core/PropertyRegistry.hpp>
#include <Util/Logger.hpp>
#include <cmath>
#include <utility>

static const bool s_toolRegistered = [] {
    using namespace PropertyRegistry;
    PropertyDesc handle = custom("Handle", PropType::String,
        [](Instance* instance) {
            return PropValue(static_cast<Tool*>(instance)->getHandlePath());
        },
        [](Instance* instance, const PropValue& value) {
            static_cast<Tool*>(instance)->setHandlePath(std::get<std::string>(value));
        });
    handle.omitEmpty();
    handle.instanceRefClass = "BaseCube";
    handle.editorWidget = EditorWidget::InstanceReference;

    registerClass("Tool", "Model", {
        sig<&Tool::Activated>("Activated"),
        field<&Tool::Equipped>("Equipped").luaReadOnly().noEditor().noClone().noYaml(),
        enumProp<&Tool::Hand>("Hand", {
            {"Right", 0}, {"Left", 1}, {"Both", 2}
        }, true),
        field<&Tool::GripC0>("GripC0"),
        field<&Tool::GripC1>("GripC1"),
        handle,
    });
    return true;
}();

Tool::Tool(std::string name) : Model() {
    Name = std::move(name);
    Activated = std::make_shared<RCBNScriptSignal>();
}

void Tool::setHandleReference(const std::shared_ptr<BaseCube>& handle) {
    Handle = handle;
    m_handleName = handle ? handle->getWorkspaceRelativePath() : std::string{};
}

void Tool::setHandlePath(const std::string& path) {
    m_handleName = path;
    resolveHandle();
}

void Tool::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "Tool", name, value)) return;
    if (name == "Hand") {
        std::string s = value.as<std::string>();
        Hand = (s == "Left") ? ToolHand::Left : (s == "Both") ? ToolHand::Both : ToolHand::Right;
        return;
    }
    if (name == "Handle") {
        m_handleName = value.as<std::string>();
        resolveHandle();
        return;
    }
    Instance::setProperty(name, value);
}

void Tool::setLegacyGripOffset(const CFrame& legacyOffset) {
    GripC1 = legacyOffset.inverse();
}

std::shared_ptr<Instance> Tool::clone() const {
    auto copy = std::make_shared<Tool>(Name);
    copy->Size = Size;
    copy->setCFrame(getCFrame());
    PropertyRegistry::cloneFields(this, copy.get(), "Tool");
    for (auto const& [name, child] : children) copy->addChild(child->clone());
    return copy;
}

void Tool::remapClonedInstances(const CloneRemap& map) {
    if (!Handle) return;
    const auto it = map.find(Handle.get());
    if (it != map.end()) {
        Handle = std::static_pointer_cast<BaseCube>(it->second);
        m_handleName = Handle->getWorkspaceRelativePath();
    }
}

void Tool::resolveHandle() {
    if (m_handleName.empty()) { Handle = nullptr; return; }
    // 解決の起点は cubeRelativePath（Editor 側）の規約と一致させる:
    // Workspace 配下なら Workspace、そうでなければ最上位祖先を起点にする。
    Instance* base = findFirstAncestorWorkspace();
    if (!base) {
        base = this;
        for (auto p = Parent.lock(); p; p = p->Parent.lock()) base = p.get();
    }
    Instance* child = base->getChildByPath(m_handleName);
    if (child && child->IsA("BaseCube"))
        Handle = std::static_pointer_cast<BaseCube>(child->shared_from_this());
}

void Tool::onAncestorChanged() {
    // Tool や Handle が移動したときは、解決済みの参照から最新の
    // Workspace 相対パスを保存し、移動後のパスで再解決する。
    // Handle が未解決の場合は、読み込み直後などの遅延解決を従来通り行う。
    // null のときには既存の m_handleName を消さない。
    if (Handle) {
        m_handleName = Handle->getWorkspaceRelativePath();
        resolveHandle();
    } else if (!m_handleName.empty()) {
        resolveHandle();
    }

    // Inventory に直接収納された Tool は、User のホットバーへ自動登録する。
    // これはシーン読み込み時の syncToolsFromInventory() だけでは拾えない、
    // Workspace からのドラッグ移動やスクリプトによる addChild を補う。
    bool isInInventory = false;
    if (auto inventory = Parent.lock()) {
        if (auto userInst = inventory->Parent.lock();
            userInst && userInst->IsA("User")) {
            auto user = std::static_pointer_cast<User>(userInst);
            if (user->Inventory.get() == inventory.get()) {
                isInInventory = true;
                m_inventoryOwner = user;
                user->addToolToSlot(std::static_pointer_cast<Tool>(shared_from_this()));
            }
        }
    }
    if (!isInInventory) {
        if (auto user = m_inventoryOwner.lock()) {
            // 装備処理では Inventory -> Character へ移しても同じスロットを維持する。
            // それ以外（EditorでWorkspaceへ移動等）は即座に強参照を破棄する。
            if (user->currentTool.get() != this) {
                user->removeToolReferences(
                    std::static_pointer_cast<Tool>(shared_from_this()));
                m_inventoryOwner.reset();
            }
        }
    }
    Instance::onAncestorChanged();
}
