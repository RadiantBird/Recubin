#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <cstring>

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

    static int instance_index(lua_State* L) {
        Instance* obj = *(Instance**)luaL_checkudata(L, 1, RCBN_INST_METATABLE);
        const char* key = luaL_checkstring(L, 2);

        if (strcmp(key, "Name") == 0) {
            std::cout << "Printing Name...\n";
            lua_pushstring(L, obj->Name.c_str());
            return 1;
        }
        
        if (obj->IsA("BaseCube")) {
            auto basecube = dynamic_cast<BaseCube*>(obj);

            if (strcmp(key, "Position") == 0) {
                std::cout << "Printing Position...\n";
                Vector3 v = basecube->Position;
                lua_pushstring(L, v.toString().c_str());
                return 1;
            }
        }
        return NIL;
    }

public:
    LuauEngine() {
        L = luaL_newstate();
        luaL_openlibs(L);
        
        luaL_newmetatable(L, RCBN_INST_METATABLE);
        
        lua_pushcfunction(L, instance_index, "instance_index");
        lua_setfield(L, -2, "__index"); // mt.__index = instance_index

        lua_pushcfunction(L, instance_tostring, "instance_tostring");
        lua_setfield(L, -2, "__tostring");
        
        lua_pop(L, 1); // メタテーブルをスタックから片付ける
    }

    ~LuauEngine() {
        if (L) lua_close(L);
    }

    void setBindings(Instance* instance) {
        Instance** userdata = (Instance**)lua_newuserdata(L, sizeof(Instance*));
        *userdata = instance;

        luaL_getmetatable(L, RCBN_INST_METATABLE);
        lua_setmetatable(L, -2);
    }

    void setGlobalInstance(const std::string& name, Instance* instance) {
        setBindings(instance);
        lua_setglobal(L, name.c_str());
    }

    static int instance_tostring(lua_State* L) {
        Instance* obj = *(Instance**)luaL_checkudata(L, 1, RCBN_INST_METATABLE);
        std::string str = "Instance: " + obj->Name;
        lua_pushstring(L, str.c_str());
        return 1;
    }

    bool execute(const std::string& source) {
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
};