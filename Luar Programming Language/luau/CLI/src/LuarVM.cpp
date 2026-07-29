// Minimal Luau runner for testing the Luar standard library (table.deepcopy etc.)
#include "lua.h"
#include "lualib.h"
#include "Luau/Compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>

static std::string readFile(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return {};
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0)
    {
        fclose(f);
        return {};
    }
    std::string buf(size, '\0');
    fread(&buf[0], 1, (size_t)size, f);
    fclose(f);
    return buf;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: luarvm <script.luau>\n");
        return 1;
    }

    std::string source = readFile(argv[1]);
    if (source.empty())
    {
        fprintf(stderr, "luarvm: cannot open '%s'\n", argv[1]);
        return 1;
    }

    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    luaL_sandbox(L);

    lua_State* T = lua_newthread(L);
    luaL_sandboxthread(T);

    Luau::CompileOptions opts{};
    std::string bytecode = Luau::compile(source, opts);

    if (luau_load(T, argv[1], bytecode.data(), bytecode.size(), 0) != 0)
    {
        fprintf(stderr, "luarvm: compile error: %s\n", lua_tostring(T, -1));
        lua_close(L);
        return 1;
    }

    int status = lua_resume(T, NULL, 0);
    if (status != LUA_OK && status != LUA_YIELD)
    {
        const char* msg = lua_tostring(T, -1);
        fprintf(stderr, "luarvm: runtime error: %s\n", msg ? msg : "(unknown error)");
        lua_close(L);
        return 1;
    }

    lua_close(L);
    return 0;
}
