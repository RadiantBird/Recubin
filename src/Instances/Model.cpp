#include <include/Instances/Model.hpp>
#include <include/Util/Logger.hpp>
#include <atomic>
#include <limits>

namespace {
std::atomic<std::uint32_t> s_nextCharacterCollisionGroup{1};

std::uint32_t allocateCharacterCollisionGroup() {
    constexpr std::uint32_t MAX_GROUP =
        static_cast<std::uint32_t>(std::numeric_limits<int>::max());
    std::uint32_t next =
        s_nextCharacterCollisionGroup.load(std::memory_order_relaxed);
    while (next <= MAX_GROUP) {
        if (s_nextCharacterCollisionGroup.compare_exchange_weak(
                next, next + 1, std::memory_order_relaxed))
            return next;
    }
    RCBN_ERROR("Character collision group ID space exhausted");
    return 0;
}
}

void Model::refreshCharacterCollisionGroup() {
    bool hasDirectHumanoid = false;
    for (const auto& [_, child] : children) {
        if (child && child->IsA("Humanoid")) {
            hasDirectHumanoid = true;
            break;
        }
    }

    const std::uint32_t oldGroup = m_characterCollisionGroup;
    if (hasDirectHumanoid && m_characterCollisionGroup == 0)
        m_characterCollisionGroup = allocateCharacterCollisionGroup();
    else if (!hasDirectHumanoid)
        m_characterCollisionGroup = 0;

    if (oldGroup == m_characterCollisionGroup) return;
    for (const auto& [_, child] : children) {
        if (child) child->onAncestorChanged();
    }
}

void Model::onChildrenChanged() {
    refreshCharacterCollisionGroup();
}

std::shared_ptr<Instance> Model::clone() const {
    auto copy = std::make_shared<Model>(Position, Size);
    copy->Name = Name;
    copy->cframe = cframe;

    for (auto const& [name, child] : children) {
        copy->addChild(child->clone());
    }

    return copy;
}
