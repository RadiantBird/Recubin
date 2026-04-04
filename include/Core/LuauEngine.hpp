#pragma once
#include <iostream>
#include <string>
#include <vector>

#include "include/luau/lua.h"
#include "include/luau/lualib.h"
#include "include/luau/luacode.h"

// ライブラリのパスが通っていない可能性があるので、ここに追加
#pragma comment(lib, "Luau.VM.lib")
#pragma comment(lib, "Luau.Compiler.lib")
#pragma comment(lib, "Luau.Ast.lib")
#pragma comment(lib, "Luau.Common.lib")

class LuauEngine {
private:
    lua_State* L;

public:
    LuauEngine() {
        L = luaL_newstate();
        luaL_openlibs(L);
    }

    ~LuauEngine() {
        if (L) lua_close(L);
    }

    // ソースコード（日本語コメント入りOK）を食わせて実行する
    bool execute(const std::string& source) {
        size_t bytecodeSize = 0;
        // 1. コンパイル（ここで一瞬 char* を使うが、すぐ解放する）
        char* bytecode = luau_compile(source.c_str(), source.length(), nullptr, &bytecodeSize);
        
        if (!bytecode) return false;

        // 2. VMにロード (luau_load は内部でコピーを取るので安全)
        int status = luau_load(L, "@RecubinTask", bytecode, bytecodeSize, 0);
        free(bytecode); // 速攻で生のポインタを捨てる

        if (status != 0) {
            std::cerr << "Luau Load Error: " << lua_tostring(L, -1) << "\n";
            lua_pop(L, 1);
            return false;
        }

        // 3. 実行（pcall: 保護付き呼び出し）
        if (lua_pcall(L, 0, 0, 0) != 0) {
            std::cerr << "Luau Run Error: " << lua_tostring(L, -1) << "\n";
            lua_pop(L, 1);
            return false;
        }

        return true;
    }
};