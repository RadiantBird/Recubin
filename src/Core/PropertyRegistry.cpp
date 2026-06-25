#include "include/Core/PropertyRegistry.hpp"

namespace PropertyRegistry {

// 静的初期化順序に依存しないよう関数ローカル静的で保持する
static std::unordered_map<std::string_view, std::vector<PropertyDesc>>& registry() {
    static std::unordered_map<std::string_view, std::vector<PropertyDesc>> s_registry;
    return s_registry;
}

void registerClass(std::string_view className, std::vector<PropertyDesc> props) {
    registry()[className] = std::move(props);  // 再登録は上書き（冪等）
}

bool loadProperty(Instance* obj, std::string_view className,
                  const std::string& name, const YAML::Node& value) {
    auto it = registry().find(className);
    if (it == registry().end()) return false;
    for (const auto& p : it->second) {
        if (p.load && p.name == name) {
            p.load(obj, value);
            return true;
        }
    }
    return false;
}

void saveProperties(YAML::Emitter& out, const Instance* obj, std::string_view className) {
    auto it = registry().find(className);
    if (it == registry().end()) return;
    for (const auto& p : it->second)
        if (p.save) p.save(out, obj);
}

void cloneProperties(const Instance* src, Instance* dst, std::string_view className) {
    auto it = registry().find(className);
    if (it == registry().end()) return;
    for (const auto& p : it->second)
        if (p.copy) p.copy(src, dst);
}

void applyToDispatch(std::string_view className, GetterMap& getters, SetterMap& setters) {
    auto it = registry().find(className);
    if (it == registry().end()) return;
    for (const auto& p : it->second) {
        if (p.luaGet) getters[className][p.name] = p.luaGet;
        if (p.luaSet) setters[className][p.name] = p.luaSet;
    }
}

} // namespace PropertyRegistry
