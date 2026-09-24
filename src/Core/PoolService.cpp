#include "include/Core/PoolService.hpp"

#include "include/Instances/Instance.hpp"
#include "include/Util/Logger.hpp"

namespace {
const std::unordered_map<std::string, bool>& poolWhitelist() {
    static const std::unordered_map<std::string, bool> whitelist = {
        {"Cube", true}, {"Cylinder", true}, {"TriangularPrism", true},
        {"Truss", true}, {"Seat", true}, {"Sphere", true},
        {"MeshCube", true}, {"LiquidCube", true},
    };
    return whitelist;
}
}

bool PoolService::supports(const std::string& className) const {
    return poolWhitelist().find(className) != poolWhitelist().end();
}

std::shared_ptr<Instance> PoolService::serveObject(const std::string& className,
                                                    const Factory& factory) {
    if (!supports(className)) return nullptr;
    if (!factory) {
        RCBN_ERROR("PoolService: no factory for whitelisted class '" << className << "'");
        return nullptr;
    }

    auto& available = m_pool[className];
    std::shared_ptr<Instance> instance;
    if (!available.empty()) {
        instance = std::move(available.back());
        available.pop_back();
    } else {
        instance = factory();
    }
    if (!instance) {
        RCBN_ERROR("PoolService: factory failed for class '" << className << "'");
        return nullptr;
    }

    instance->init();
    m_active[instance.get()] = instance;
    return instance;
}

bool PoolService::releaseObject(const std::shared_ptr<Instance>& instance) {
    if (!instance) {
        RCBN_WARN("PoolService: throw rejected because Instance is null");
        return false;
    }

    const std::string className = instance->getClassName();
    if (!supports(className)) {
        RCBN_WARN("PoolService: throw rejected for unsupported class '" << className << "'");
        return false;
    }
    if (!instance->Parent.expired()) {
        // Luauの使用例ではParentを設定した後にthrowする。返却時に
        // Workspaceの描画・物理登録を通常経路で解除してから保管する。
        instance->setParent(nullptr);
    }
    if (!instance->children.empty()) {
        RCBN_WARN("PoolService: throw rejected for Instance with children '"
                  << instance->Name << "'");
        return false;
    }

    auto active = m_active.find(instance.get());
    if (active == m_active.end() || active->second.get() != instance.get()) {
        RCBN_WARN("PoolService: throw rejected for Instance not borrowed from the pool ('"
                  << instance->Name << "')");
        return false;
    }

    auto owned = std::move(active->second);
    m_active.erase(active);
    owned->init();
    m_pool[className].push_back(std::move(owned));
    return true;
}
