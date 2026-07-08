#include <include/Instances/Attachment.hpp>

std::shared_ptr<Instance> Attachment::clone() const {
    auto c = std::make_shared<Attachment>();
    c->Name   = Name;
    c->cframe = cframe;
    c->Size   = Size;
    for (auto const& [n, ch] : children) c->addChild(ch->clone());
    return c;
}

std::shared_ptr<Attachment> Attachment::findUnder(Instance* root, const std::string& path) {
    if (!root || path.empty()) return nullptr;
    Instance* found = root->getChildByPath(path);
    if (found && found->IsA("Attachment"))
        return std::static_pointer_cast<Attachment>(found->shared_from_this());
    return nullptr;
}

CFrame Attachment::relativeToAncestor(const Instance* ancestor) const {
    CFrame rel = cframe;
    for (auto p = Parent.lock(); p; p = p->Parent.lock()) {
        if (p.get() == ancestor) return rel;
        if (p->IsA("Spatial"))
            rel = static_cast<const Spatial*>(p.get())->cframe * rel;
    }
    return cframe; // ancestor が祖先に無い場合のフォールバック
}
