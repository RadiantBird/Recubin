#include <Instances/Tool.hpp>
#include <Core/User.hpp>

Tool::Tool(std::string name) : Instance(name) {
    Activated = std::make_shared<RCBNScriptSignal>();
}

void Tool::setProperty(const std::string& name, const YAML::Node& value) {
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
    // ロード直後など、まだ Handle が見つからない場合に祖先確定後の再解決を試みる
    if (!Handle && !m_handleName.empty()) resolveHandle();
    Instance::onAncestorChanged();
}