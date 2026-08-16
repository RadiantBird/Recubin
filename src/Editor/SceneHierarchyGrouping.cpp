#include <Editor/SceneHierarchyGrouping.hpp>

GroupInstancesCommand::GroupInstancesCommand(
    std::shared_ptr<Instance> parent,
    std::shared_ptr<Instance> group,
    std::vector<std::shared_ptr<Instance>> children)
    : m_parent(std::move(parent)), m_group(std::move(group)) {
    for (auto& child : children) {
        if (!child) continue;
        Entry entry{child, child->Parent.lock(), {}};
        std::function<void(const std::shared_ptr<Instance>&)> capture = [&](const std::shared_ptr<Instance>& node) {
            if (auto spatial = std::dynamic_pointer_cast<Spatial>(node))
                entry.poses.push_back({spatial, spatial->getWorldCFrame()});
            for (const auto& [name, descendant] : node->children)
                if (descendant) capture(descendant);
        };
        capture(child);
        m_entries.push_back(std::move(entry));
    }
}

void GroupInstancesCommand::execute() {
    if (!m_parent || !m_group) return;
    m_parent->addChild(m_group);
    for (auto& entry : m_entries) {
        if (!entry.child) continue;
        if (entry.oldParent) entry.oldParent->removeChild(entry.child->Name);
        m_group->addChild(entry.child);
        for (auto& pose : entry.poses) pose.target->setWorldCFrame(pose.world);
    }
}

void GroupInstancesCommand::undo() {
    if (!m_parent || !m_group) return;
    for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it) {
        if (!it->child) continue;
        m_group->removeChild(it->child->Name);
        if (it->oldParent) {
            it->oldParent->addChild(it->child);
            for (auto& pose : it->poses) pose.target->setWorldCFrame(pose.world);
        }
    }
    m_parent->removeChild(m_group->Name);
}
