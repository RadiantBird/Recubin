#include <include/Instances/Attachment.hpp>

std::shared_ptr<Instance> Attachment::clone() const {
    auto c = std::make_shared<Attachment>();
    c->Name   = Name;
    c->setCFrame(getCFrame());
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

std::shared_ptr<Attachment> Attachment::resolveReference(Instance* cube, const std::string& path) {
    if (!cube || path.empty()) return nullptr;
    if (auto found = findUnder(cube, path)) return found;

    Instance* base = cube->findFirstAncestorWorkspace();
    if (!base) {
        base = cube;
        for (auto p = cube->Parent.lock(); p; p = p->Parent.lock()) base = p.get();
    }
    auto found = findUnder(base, path);
    if (!found) return nullptr;
    for (auto p = found->Parent.lock(); p; p = p->Parent.lock()) {
        if (p.get() == cube) return found;
    }
    return nullptr;
}

std::shared_ptr<Attachment> Attachment::createAtParent(const Instance& parent) {
    auto attachment = std::make_shared<Attachment>();
    const Instance* node = &parent;
    while (node) {
        if (const auto* spatial = dynamic_cast<const Spatial*>(node)) {
            attachment->setCFrame(spatial->getWorldCFrame());
            break;
        }
        auto next = node->Parent.lock();
        node = next.get();
    }
    return attachment;
}

CFrame Attachment::relativeToAncestor(const Instance* ancestor) const {
    CFrame rel = getCFrame();
    for (auto p = Parent.lock(); p; p = p->Parent.lock()) {
        if (p.get() == ancestor) return rel;
        if (p->IsA("Spatial"))
            rel = static_cast<const Spatial*>(p.get())->getCFrame() * rel;
    }
    return getCFrame(); // ancestor が祖先に無い場合のフォールバック
}
