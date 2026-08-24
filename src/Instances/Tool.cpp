#include <Instances/Tool.hpp>
#include <Core/User.hpp>

Tool::Tool(std::string name) : Instance(name) {
    Activated = std::make_shared<RCBNScriptSignal>();
}

void Tool::setHandleReference(const std::shared_ptr<BaseCube>& handle) {
    Handle = handle;
    m_handleName = handle ? handle->getWorkspaceRelativePath() : std::string{};
}

void Tool::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "Position" && value.IsSequence() && value.size() == 3) {
        Position = Vector3(value[0].as<float>(), value[1].as<float>(), value[2].as<float>());
        return;
    }
    if (name == "Rotation" && value.IsSequence() && value.size() == 4) {
        Rotation = Quaternion(value[3].as<float>(), value[0].as<float>(),
                              value[1].as<float>(), value[2].as<float>());
        return;
    }
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
