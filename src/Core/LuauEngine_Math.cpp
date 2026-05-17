#include "include/Core/LuauEngine.hpp"

// ==================== Vector3 Methods ====================
int LuauEngine::vec3_constructor(lua_State* L) {
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float z = (float)luaL_checknumber(L, 3);

    Vector3* vec = (Vector3*)lua_newuserdata(L, sizeof(Vector3));
    *vec = Vector3(x, y, z);

    luaL_getmetatable(L, RCBN_VEC3_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

int LuauEngine::vec3_index(lua_State* L) {
    Vector3* vec = (Vector3*)luaL_checkudata(L, 1, RCBN_VEC3_METATABLE);
    std::string_view key = luaL_checkstring(L, 2);

    if (key == "x") {
        lua_pushnumber(L, vec->x);
        return 1;
    } else if (key == "y") {
        lua_pushnumber(L, vec->y);
        return 1;
    } else if (key == "z") {
        lua_pushnumber(L, vec->z);
        return 1;
    } else if (key == "length") {
        lua_pushcfunction(L, [](lua_State* L) -> int {
            Vector3* v = (Vector3*)luaL_checkudata(L, 1, RCBN_VEC3_METATABLE);
            lua_pushnumber(L, v->length());
            return 1;
        }, "length");
        return 1;
    } else if (key == "normalize") {
        lua_pushcfunction(L, [](lua_State* L) -> int {
            Vector3* v = (Vector3*)luaL_checkudata(L, 1, RCBN_VEC3_METATABLE);
            Vector3 normalized = v->normalize();
            Vector3* result = (Vector3*)lua_newuserdata(L, sizeof(Vector3));
            *result = normalized;
            luaL_getmetatable(L, RCBN_VEC3_METATABLE);
            lua_setmetatable(L, -2);
            return 1;
        }, "normalize");
        return 1;
    }

    return 0;
}

int LuauEngine::vec3_newindex(lua_State* L) {
    Vector3* vec = (Vector3*)luaL_checkudata(L, 1, RCBN_VEC3_METATABLE);
    std::string_view key = luaL_checkstring(L, 2);
    float value = (float)luaL_checknumber(L, 3);

    if (key == "x") {
        vec->x = value;
    } else if (key == "y") {
        vec->y = value;
    } else if (key == "z") {
        vec->z = value;
    }

    return 0;
}

int LuauEngine::vec3_tostring(lua_State* L) {
    Vector3* vec = (Vector3*)luaL_checkudata(L, 1, RCBN_VEC3_METATABLE);
    std::string str = "Vector3(" + vec->toString() + ")";
    lua_pushstring(L, str.c_str());
    return 1;
}

// ==================== Color4 Methods ====================
int LuauEngine::color4_constructor(lua_State* L) {
    float r = (float)luaL_checknumber(L, 1);
    float g = (float)luaL_checknumber(L, 2);
    float b = (float)luaL_checknumber(L, 3);
    float a = lua_isnumber(L, 4) ? (float)lua_tonumber(L, 4) : 1.0f;

    Color4* color = (Color4*)lua_newuserdata(L, sizeof(Color4));
    *color = Color4(r, g, b, a);

    luaL_getmetatable(L, RCBN_COLOR4_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

int LuauEngine::color4_index(lua_State* L) {
    Color4* color = (Color4*)luaL_checkudata(L, 1, RCBN_COLOR4_METATABLE);
    std::string_view key = luaL_checkstring(L, 2);

    if (key == "r") {
        lua_pushnumber(L, color->r);
        return 1;
    } else if (key == "g") {
        lua_pushnumber(L, color->g);
        return 1;
    } else if (key == "b") {
        lua_pushnumber(L, color->b);
        return 1;
    } else if (key == "a") {
        lua_pushnumber(L, color->a);
        return 1;
    }

    return 0;
}

int LuauEngine::color4_newindex(lua_State* L) {
    Color4* color = (Color4*)luaL_checkudata(L, 1, RCBN_COLOR4_METATABLE);
    std::string_view key = luaL_checkstring(L, 2);
    float value = (float)luaL_checknumber(L, 3);

    if (key == "r") {
        color->r = value;
    } else if (key == "g") {
        color->g = value;
    } else if (key == "b") {
        color->b = value;
    } else if (key == "a") {
        color->a = value;
    }

    return 0;
}

int LuauEngine::color4_tostring(lua_State* L) {
    Color4* color = (Color4*)luaL_checkudata(L, 1, RCBN_COLOR4_METATABLE);
    std::string str = "Color4(" + std::to_string(color->r) + ", " + std::to_string(color->g) + ", " +
                      std::to_string(color->b) + ", " + std::to_string(color->a) + ")";
    lua_pushstring(L, str.c_str());
    return 1;
}

// ==================== Vector2 Methods ====================
int LuauEngine::vec2_constructor(lua_State* L) {
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);

    Vector2* vec = (Vector2*)lua_newuserdata(L, sizeof(Vector2));
    *vec = Vector2(x, y);

    luaL_getmetatable(L, RCBN_VEC2_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

int LuauEngine::vec2_index(lua_State* L) {
    Vector2* vec = (Vector2*)luaL_checkudata(L, 1, RCBN_VEC2_METATABLE);
    std::string_view key = luaL_checkstring(L, 2);

    if (key == "X" || key == "x") {
        lua_pushnumber(L, vec->x);
        return 1;
    } else if (key == "Y" || key == "y") {
        lua_pushnumber(L, vec->y);
        return 1;
    }
    return 0;
}

int LuauEngine::vec2_newindex(lua_State* L) {
    Vector2* vec = (Vector2*)luaL_checkudata(L, 1, RCBN_VEC2_METATABLE);
    std::string_view key = luaL_checkstring(L, 2);
    float value = (float)luaL_checknumber(L, 3);

    if (key == "X" || key == "x") {
        vec->x = value;
    } else if (key == "Y" || key == "y") {
        vec->y = value;
    }
    return 0;
}

int LuauEngine::vec2_tostring(lua_State* L) {
    Vector2* vec = (Vector2*)luaL_checkudata(L, 1, RCBN_VEC2_METATABLE);
    std::string str = "Vector2(" + vec->toString() + ")";
    lua_pushstring(L, str.c_str());
    return 1;
}
