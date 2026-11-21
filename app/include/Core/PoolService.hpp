#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Instance;

// Instance の親子ツリーが shared_ptr を契約としているため、pool も
// shared_ptr を保持する。vector の再配置で Instance 本体は移動しない。
class PoolService {
public:
    using Factory = std::function<std::shared_ptr<Instance>()>;

    std::shared_ptr<Instance> serveObject(const std::string& className,
                                          const Factory& factory);
    bool releaseObject(const std::shared_ptr<Instance>& instance);
    bool supports(const std::string& className) const;

private:
    std::unordered_map<std::string, std::vector<std::shared_ptr<Instance>>> m_pool;
    std::unordered_map<Instance*, std::shared_ptr<Instance>> m_active;
};
