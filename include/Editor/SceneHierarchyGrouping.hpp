#pragma once

#include <Editor/CommandHistory.hpp>
#include <Instances/Spatial.hpp>
#include <memory>
#include <optional>
#include <functional>
#include <vector>

// One undoable operation for creating a container and moving a set of instances
// into it.  World transforms are captured before the move and restored on both
// execute and undo, so grouping never changes the visible scene.
class GroupInstancesCommand final : public Command {
public:
    GroupInstancesCommand(std::shared_ptr<Instance> parent,
                          std::shared_ptr<Instance> group,
                          std::vector<std::shared_ptr<Instance>> children);

    void execute() override;
    void undo() override;

private:
    struct Entry {
        std::shared_ptr<Instance> child;
        std::shared_ptr<Instance> oldParent;
        struct SpatialPose { std::shared_ptr<Spatial> target; CFrame world; };
        std::vector<SpatialPose> poses;
    };
    std::shared_ptr<Instance> m_parent;
    std::shared_ptr<Instance> m_group;
    std::vector<Entry> m_entries;
};
