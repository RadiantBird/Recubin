#pragma once
#include <iostream>
#include <string>
#include <unordered_map>
#include <cstring>
#include <string_view>
#include <functional>

#include "include/luau/lua.h"
#include "include/luau/lualib.h"
#include "include/luau/luacode.h"

#include "include/Instances/Instance.hpp"
#include "include/Instances/BaseCube.hpp"

#pragma comment(lib, "Luau.VM.lib")
#pragma comment(lib, "Luau.Compiler.lib")
#pragma comment(lib, "Luau.Ast.lib")
#pragma comment(lib, "Luau.Common.lib")

class LuauEngine {
private:
    lua_State* L;
    static constexpr const char* RCBN_INST_METATABLE = "RCBN_Instance";
    static constexpr const int NIL = 0;

public:
    using GetterFunc = std::function<int(lua_State*, Instance*)>;

private:
    static std::unordered_map<std::string_view, std::unordered_map<std::string_view, GetterFunc>> DispatchTable;

    void InitDispatchTable();

    static int instance_index(lua_State* L);
    static int instance_tostring(lua_State* L);

public:
    LuauEngine();
    ~LuauEngine();

    void setBindings(Instance* instance);

    void setGlobalInstance(const std::string& name, Instance* instance);

    bool execute(const std::string& source);
};
