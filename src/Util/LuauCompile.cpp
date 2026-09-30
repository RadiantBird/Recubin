#include <Util/LuauCompile.hpp>
#include "include/luau/luacode.h"

#include <algorithm>

namespace LuauCompile {

const std::vector<std::string>& instanceGlobals() {
    // SceneRuntime/LuauEngineがsetGlobalInstanceやlua_setglobalで公開するInstance。
    static const std::vector<std::string> names = {
        "workspace", "script", "System", "system", "User", "ChatService"};
    return names;
}

char* compile(const std::string& source, size_t& bytecodeSize,
              const std::vector<std::string>& extraMutableGlobals) {
    std::vector<std::string> names = instanceGlobals();
    for (const std::string& name : extraMutableGlobals) {
        if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
    }
    std::vector<const char*> pointers;
    pointers.reserve(names.size() + 1);
    for (const std::string& name : names) pointers.push_back(name.c_str());
    pointers.push_back(nullptr);

    lua_CompileOptions options = {};
    options.optimizationLevel = 1;  // luacode.hの既定値
    options.debugLevel = 1;         // luacode.hの既定値
    options.mutableGlobals = pointers.data();
    return luau_compile(source.c_str(), source.size(), &options, &bytecodeSize);
}

} // namespace LuauCompile
