#include "include/Core/LuauEngine.hpp"

// DispatchTableの定義
std::unordered_map<std::string_view, std::unordered_map<std::string_view, LuauEngine::GetterFunc>> LuauEngine::DispatchTable;

void LuauEngine::InitDispatchTable() {
    DispatchTable["Instance"]["Name"] = [](lua_State* L, Instance* obj) {
        lua_pushstring(L, obj->Name.c_str());
        return 1;
    };

    DispatchTable["BaseCube"]["Position"] = [](lua_State* L, Instance* obj) {
        auto cube = static_cast<BaseCube*>(obj);
        lua_pushstring(L, cube->Position.toString().c_str());
        return 1;
    };
}

int LuauEngine::instance_index(lua_State* L) {
    Instance* obj = *(Instance**)luaL_checkudata(L, 1, RCBN_INST_METATABLE);
    std::string_view key = luaL_checkstring(L, 2);

    for (const auto& [className, classProps] : DispatchTable) {
        if (obj->IsA(std::string(className))) {
            if (auto it = classProps.find(key); it != classProps.end()) {
                auto& [name, resolveProperty] = *it;
                return resolveProperty(L, obj);
            }
        }
    }

    return NIL;
}

LuauEngine::LuauEngine() {
    L = luaL_newstate();
    luaL_openlibs(L);

    luaL_newmetatable(L, RCBN_INST_METATABLE);

    lua_pushcfunction(L, instance_index, "instance_index");
    lua_setfield(L, -2, "__index"); // mt.__index = instance_index

    lua_pushcfunction(L, instance_tostring, "instance_tostring");
    lua_setfield(L, -2, "__tostring");

    lua_pop(L, 1); // メタテーブルをスタックから片付ける
    InitDispatchTable();
}

LuauEngine::~LuauEngine() {
    if (L) lua_close(L);
}

void LuauEngine::setBindings(Instance* instance) {
    Instance** userdata = (Instance**)lua_newuserdata(L, sizeof(Instance*));
    *userdata = instance;

    luaL_getmetatable(L, RCBN_INST_METATABLE);
    lua_setmetatable(L, -2);
}

void LuauEngine::setGlobalInstance(const std::string& name, Instance* instance) {
    setBindings(instance);
    lua_setglobal(L, name.c_str());
}

int LuauEngine::instance_tostring(lua_State* L) {
    Instance* obj = *(Instance**)luaL_checkudata(L, 1, RCBN_INST_METATABLE);
    std::string str = "Instance: " + obj->Name;
    lua_pushstring(L, str.c_str());
    return 1;
}

bool LuauEngine::execute(const std::string& source) {
    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(source.c_str(), source.length(), nullptr, &bytecodeSize);
    if (!bytecode) return false;

    int status = luau_load(L, "@RecubinTask", bytecode, bytecodeSize, 0);
    free(bytecode);

    if (status != 0) {
        std::cerr << "Luau Load Error: " << lua_tostring(L, -1) << "\n";
        lua_pop(L, 1);
        return false;
    }

    if (lua_pcall(L, 0, 0, 0) != 0) {
        std::cerr << "Luau Run Error: " << lua_tostring(L, -1) << "\n";
        lua_pop(L, 1);
        return false;
    }
    return true;
}