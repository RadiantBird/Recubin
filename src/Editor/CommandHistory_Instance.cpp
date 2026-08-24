#include <Editor/CommandHistory.hpp>

AddInstanceCommand::AddInstanceCommand(std::shared_ptr<Instance> parent, std::shared_ptr<Instance> child)
    : m_parent(std::move(parent)), m_child(std::move(child)) {}
void AddInstanceCommand::execute() { if (m_parent && m_child) m_parent->addChild(m_child); }
void AddInstanceCommand::undo() { if (m_parent && m_child) m_parent->removeChild(m_child->Name); }

RemoveInstanceCommand::RemoveInstanceCommand(std::shared_ptr<Instance> parent, std::string name,
                                             std::shared_ptr<Instance> child)
    : m_parent(std::move(parent)), m_name(std::move(name)), m_child(std::move(child)) {}
void RemoveInstanceCommand::execute() { if (m_parent) m_parent->removeChild(m_name); }
void RemoveInstanceCommand::undo() { if (m_parent && m_child) m_parent->addChild(m_child); }

MoveInstanceCommand::MoveInstanceCommand(std::shared_ptr<Instance> oldParent,
    std::shared_ptr<Instance> newParent, std::shared_ptr<Instance> child)
    : m_oldParent(std::move(oldParent)), m_newParent(std::move(newParent)), m_child(std::move(child)) {}
void MoveInstanceCommand::execute() {
    if (m_oldParent) m_oldParent->removeChild(m_child->Name);
    if (m_newParent) m_newParent->addChild(m_child);
}
void MoveInstanceCommand::undo() {
    if (m_newParent) m_newParent->removeChild(m_child->Name);
    if (m_oldParent) m_oldParent->addChild(m_child);
}

ReplaceInstanceCommand::ReplaceInstanceCommand(std::shared_ptr<Instance> parent,
                                               std::shared_ptr<Instance> before,
                                               std::string className,
                                               std::function<void(Instance*)> onSelection,
                                               std::shared_ptr<Instance> systemRoot)
    : m_parent(std::move(parent)), m_before(std::move(before)),
      m_className(std::move(className)), m_onSelection(std::move(onSelection)),
      m_systemRoot(std::move(systemRoot)) {}

const std::vector<std::string>& ReplaceInstanceCommand::incompatibleReferenceOwners() const {
    return m_incompatibleOwners;
}

void ReplaceInstanceCommand::analyzeReferences(const std::shared_ptr<Instance>& replacement) {
    m_referenceChanges.clear(); m_incompatibleOwners.clear();
    if (!m_systemRoot) return;
    std::function<void(const std::shared_ptr<Instance>&)> visit = [&](const std::shared_ptr<Instance>& node) {
        if (!node) return;
        std::vector<Instance::InstanceReference> refs;
        node->collectInstanceReferences(refs);
        for (auto& ref : refs) {
            if (!ref.target || ref.target.get() != m_before.get() || !ref.set) continue;
            const bool compatible = ref.requiredClass.empty() || replacement->IsA(ref.requiredClass);
            m_referenceChanges.push_back({ref.set, ref.target, compatible ? replacement : nullptr,
                                          compatible, ref.ownerLabel});
            if (!compatible) m_incompatibleOwners.push_back(ref.ownerLabel);
        }
        // Schema-declared string references (instanceRefClass) use the
        // same path contract as YAML. Resolve them before replacing and
        // restore the exact original string through the captured setter.
        for (const auto* desc : PropertyRegistry::collectSchema(node->getClassName())) {
            if (!desc || desc->instanceRefClass.empty() || !desc->get || !desc->set) continue;
            const auto value = desc->get(node.get());
            if (!std::holds_alternative<std::string>(value)) continue;
            const auto& path = std::get<std::string>(value);
            auto target = m_systemRoot->getChildByPath(path);
            if (!target) {
                // Some serialized references are Workspace-relative rather
                // than System-root-relative. Resolve them against the
                // owner's nearest Workspace using the same path contract.
                if (auto* workspace = node->findFirstAncestorWorkspace())
                    target = workspace->getChildByPath(path);
            }
            if (!target || target != m_before.get()) continue;
            const bool compatible = replacement->IsA(std::string(desc->instanceRefClass));
            const std::string beforePath = path;
            // The replacement is not parented yet during analysis. It has
            // the same name and parent slot as the old node, so compatible
            // serialized references retain their original path exactly.
            const std::string afterPath = compatible ? beforePath : std::string{};
            auto setter = [owner = node, desc, beforePath, afterPath, old = m_before](std::shared_ptr<Instance> value) {
                desc->set(owner.get(), PropValue(value ? (value == old ? beforePath : afterPath) : afterPath));
            };
            m_referenceChanges.push_back({setter, m_before, compatible ? replacement : nullptr,
                                          compatible, node->getFullPath() + "." + std::string(desc->name)});
            if (!compatible) m_incompatibleOwners.push_back(node->getFullPath() + "." + std::string(desc->name));
        }
        for (const auto& [name, child] : node->children) visit(child);
    };
    visit(m_systemRoot);
    m_referencesAnalyzed = true;
}

void ReplaceInstanceCommand::execute() {
    if (!m_parent || !m_before || m_before->Parent.lock().get() != m_parent.get()) return;
    if (!m_after) {
        m_after = SceneLoader::createInstance(m_className);
        if (!m_after) return;
        PropertyRegistry::copyCompatibleProperties(m_before.get(), m_after.get());
        m_after->Name = m_before->Name;
        analyzeReferences(m_after);
    }
    // Undo moves the exact child objects back to m_before. Move them
    // again on every execute so Redo restores child identity as well.
    std::vector<std::shared_ptr<Instance>> children;
    children.reserve(m_before->children.size());
    for (const auto& [name, child] : m_before->children)
        if (child) children.push_back(child);
    for (const auto& child : children) m_before->removeChild(child->Name);
    for (const auto& child : children) m_after->addChild(child);
    m_parent->removeChild(m_before->Name);
    m_parent->addChild(m_after);
    for (auto& ref : m_referenceChanges) ref.set(ref.after);
    if (m_onSelection) m_onSelection(m_after.get());
}

void ReplaceInstanceCommand::undo() {
    if (!m_parent || !m_before || !m_after) return;
    m_parent->removeChild(m_after->Name);
    std::vector<std::shared_ptr<Instance>> children;
    children.reserve(m_after->children.size());
    for (const auto& [name, child] : m_after->children)
        if (child) children.push_back(child);
    for (const auto& child : children) m_after->removeChild(child->Name);
    for (const auto& child : children) m_before->addChild(child);
    m_parent->addChild(m_before);
    for (auto& ref : m_referenceChanges) ref.set(ref.before);
    if (m_onSelection) m_onSelection(m_before.get());
}

void CompositeCommand::add(std::unique_ptr<Command> c) { if (c) m_cmds.push_back(std::move(c)); }
bool CompositeCommand::empty() const { return m_cmds.empty(); }
void CompositeCommand::execute() { for (auto& c : m_cmds) c->execute(); }
void CompositeCommand::undo() {
    for (auto it = m_cmds.rbegin(); it != m_cmds.rend(); ++it) (*it)->undo();
}
