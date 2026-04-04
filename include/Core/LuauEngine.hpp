#pragma once
#include <iostream>

#include "include/luau/lua.h"
#include "include/luau/lualib.h"
#include "include/luau/luacode.h"

// ライブラリのパスが通っていない可能性があるので、ここに追加
#pragma comment(lib, "Luau.VM.lib")
#pragma comment(lib, "Luau.Compiler.lib")
#pragma comment(lib, "Luau.Ast.lib")

class LuauEngine {
    public:
        void test() {
            lua_State* L = luaL_newstate();
    
            if (L) {
                std::cout << "Recubin: Luau VM initialized!\n";
                
                // 標準ライブラリ（printなど）を使えるようにする
                luaL_openlibs(L);
                
                // 使い終わったら閉じる
                lua_close(L);
            } else {
                std::cerr << "Failed to initialize Luau VM.\n";
            }
        }
};