#include "include/Core/LuauEngine.hpp"

// Luau には luaL_testudata が無いため、メタテーブル比較で型を判定する
// （Lua 5.2 の luaL_testudata 相当）。一致しなければ nullptr を返す（例外を投げない）。
static void* rcbn_testudata(lua_State* L, int idx, const char* tname) {
    void* p = lua_touserdata(L, idx);
    if (!p) return nullptr;
    if (!lua_getmetatable(L, idx)) return nullptr;  // スタックに対象の metatable
    luaL_getmetatable(L, tname);                    // スタックに登録済み metatable
    bool same = lua_rawequal(L, -1, -2) != 0;
    lua_pop(L, 2);
    return same ? p : nullptr;
}

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

int LuauEngine::vec3_add(lua_State* L) {
    Vector3* a = (Vector3*)luaL_checkudata(L, 1, RCBN_VEC3_METATABLE);
    Vector3* b = (Vector3*)luaL_checkudata(L, 2, RCBN_VEC3_METATABLE);
    pushVector3(L, *a + *b);
    return 1;
}

int LuauEngine::vec3_sub(lua_State* L) {
    Vector3* a = (Vector3*)luaL_checkudata(L, 1, RCBN_VEC3_METATABLE);
    Vector3* b = (Vector3*)luaL_checkudata(L, 2, RCBN_VEC3_METATABLE);
    pushVector3(L, *a - *b);
    return 1;
}

int LuauEngine::vec3_unm(lua_State* L) {
    Vector3* a = (Vector3*)luaL_checkudata(L, 1, RCBN_VEC3_METATABLE);
    pushVector3(L, -(*a));
    return 1;
}

int LuauEngine::vec3_mul(lua_State* L) {
    if (lua_isnumber(L, 1)) {
        float s = (float)lua_tonumber(L, 1);
        Vector3* b = (Vector3*)luaL_checkudata(L, 2, RCBN_VEC3_METATABLE);
        pushVector3(L, *b * s);
    } else {
        Vector3* a = (Vector3*)luaL_checkudata(L, 1, RCBN_VEC3_METATABLE);
        if (lua_isnumber(L, 2)) {
            pushVector3(L, *a * (float)lua_tonumber(L, 2));
        } else {
            Vector3* b = (Vector3*)luaL_checkudata(L, 2, RCBN_VEC3_METATABLE);
            pushVector3(L, *a * *b);
        }
    }
    return 1;
}

int LuauEngine::vec3_div(lua_State* L) {
    Vector3* a = (Vector3*)luaL_checkudata(L, 1, RCBN_VEC3_METATABLE);
    if (lua_isnumber(L, 2)) {
        pushVector3(L, *a / (float)lua_tonumber(L, 2));
    } else {
        Vector3* b = (Vector3*)luaL_checkudata(L, 2, RCBN_VEC3_METATABLE);
        pushVector3(L, *a / *b);
    }
    return 1;
}

int LuauEngine::vec3_eq(lua_State* L) {
    Vector3* a = (Vector3*)luaL_checkudata(L, 1, RCBN_VEC3_METATABLE);
    Vector3* b = (Vector3*)luaL_checkudata(L, 2, RCBN_VEC3_METATABLE);
    lua_pushboolean(L, *a == *b);
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

int LuauEngine::color4_add(lua_State* L) {
    Color4* a = (Color4*)luaL_checkudata(L, 1, RCBN_COLOR4_METATABLE);
    Color4* b = (Color4*)luaL_checkudata(L, 2, RCBN_COLOR4_METATABLE);
    pushColor4(L, *a + *b);
    return 1;
}

int LuauEngine::color4_sub(lua_State* L) {
    Color4* a = (Color4*)luaL_checkudata(L, 1, RCBN_COLOR4_METATABLE);
    Color4* b = (Color4*)luaL_checkudata(L, 2, RCBN_COLOR4_METATABLE);
    pushColor4(L, *a - *b);
    return 1;
}

int LuauEngine::color4_unm(lua_State* L) {
    Color4* a = (Color4*)luaL_checkudata(L, 1, RCBN_COLOR4_METATABLE);
    pushColor4(L, -(*a));
    return 1;
}

int LuauEngine::color4_mul(lua_State* L) {
    if (lua_isnumber(L, 1)) {
        float s = (float)lua_tonumber(L, 1);
        Color4* b = (Color4*)luaL_checkudata(L, 2, RCBN_COLOR4_METATABLE);
        pushColor4(L, *b * s);
    } else {
        Color4* a = (Color4*)luaL_checkudata(L, 1, RCBN_COLOR4_METATABLE);
        if (lua_isnumber(L, 2)) {
            pushColor4(L, *a * (float)lua_tonumber(L, 2));
        } else {
            Color4* b = (Color4*)luaL_checkudata(L, 2, RCBN_COLOR4_METATABLE);
            pushColor4(L, *a * *b);
        }
    }
    return 1;
}

int LuauEngine::color4_div(lua_State* L) {
    Color4* a = (Color4*)luaL_checkudata(L, 1, RCBN_COLOR4_METATABLE);
    float s = (float)luaL_checknumber(L, 2);
    pushColor4(L, *a / s);
    return 1;
}

int LuauEngine::color4_eq(lua_State* L) {
    Color4* a = (Color4*)luaL_checkudata(L, 1, RCBN_COLOR4_METATABLE);
    Color4* b = (Color4*)luaL_checkudata(L, 2, RCBN_COLOR4_METATABLE);
    lua_pushboolean(L, *a == *b);
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

int LuauEngine::vec2_add(lua_State* L) {
    Vector2* a = (Vector2*)luaL_checkudata(L, 1, RCBN_VEC2_METATABLE);
    Vector2* b = (Vector2*)luaL_checkudata(L, 2, RCBN_VEC2_METATABLE);
    pushVector2(L, *a + *b);
    return 1;
}

int LuauEngine::vec2_sub(lua_State* L) {
    Vector2* a = (Vector2*)luaL_checkudata(L, 1, RCBN_VEC2_METATABLE);
    Vector2* b = (Vector2*)luaL_checkudata(L, 2, RCBN_VEC2_METATABLE);
    pushVector2(L, *a - *b);
    return 1;
}

int LuauEngine::vec2_unm(lua_State* L) {
    Vector2* a = (Vector2*)luaL_checkudata(L, 1, RCBN_VEC2_METATABLE);
    pushVector2(L, -(*a));
    return 1;
}

int LuauEngine::vec2_mul(lua_State* L) {
    if (lua_isnumber(L, 1)) {
        float s = (float)lua_tonumber(L, 1);
        Vector2* b = (Vector2*)luaL_checkudata(L, 2, RCBN_VEC2_METATABLE);
        pushVector2(L, *b * s);
    } else {
        Vector2* a = (Vector2*)luaL_checkudata(L, 1, RCBN_VEC2_METATABLE);
        if (lua_isnumber(L, 2)) {
            pushVector2(L, *a * (float)lua_tonumber(L, 2));
        } else {
            Vector2* b = (Vector2*)luaL_checkudata(L, 2, RCBN_VEC2_METATABLE);
            pushVector2(L, *a * *b);
        }
    }
    return 1;
}

int LuauEngine::vec2_div(lua_State* L) {
    Vector2* a = (Vector2*)luaL_checkudata(L, 1, RCBN_VEC2_METATABLE);
    if (lua_isnumber(L, 2)) {
        pushVector2(L, *a / (float)lua_tonumber(L, 2));
    } else {
        Vector2* b = (Vector2*)luaL_checkudata(L, 2, RCBN_VEC2_METATABLE);
        pushVector2(L, *a / *b);
    }
    return 1;
}

int LuauEngine::vec2_eq(lua_State* L) {
    Vector2* a = (Vector2*)luaL_checkudata(L, 1, RCBN_VEC2_METATABLE);
    Vector2* b = (Vector2*)luaL_checkudata(L, 2, RCBN_VEC2_METATABLE);
    lua_pushboolean(L, *a == *b);
    return 1;
}

// ==================== Quaternion Methods ====================
// Quaternion.new(w, x, y, z) — 引数なしは単位回転
int LuauEngine::quat_constructor(lua_State* L) {
    Quaternion q;  // 既定で単位 (1,0,0,0)
    if (lua_isnumber(L, 1)) {
        float w = (float)luaL_checknumber(L, 1);
        float x = (float)luaL_checknumber(L, 2);
        float y = (float)luaL_checknumber(L, 3);
        float z = (float)luaL_checknumber(L, 4);
        q = Quaternion(w, x, y, z);
    }
    pushQuaternion(L, q);
    return 1;
}

// Quaternion.fromEuler(Vector3 degrees)
int LuauEngine::quat_from_euler(lua_State* L) {
    Vector3* deg = (Vector3*)luaL_checkudata(L, 1, RCBN_VEC3_METATABLE);
    pushQuaternion(L, Quaternion::fromEuler(*deg));
    return 1;
}

// Quaternion.fromAxisAngle(Vector3 axis, number angleDegrees)
int LuauEngine::quat_from_axis_angle(lua_State* L) {
    Vector3* axis = (Vector3*)luaL_checkudata(L, 1, RCBN_VEC3_METATABLE);
    float angle = (float)luaL_checknumber(L, 2);
    pushQuaternion(L, Quaternion::fromAxisAngle(*axis, angle));
    return 1;
}

// Quaternion.Slerp(a, b, t)
int LuauEngine::quat_slerp(lua_State* L) {
    Quaternion* a = (Quaternion*)luaL_checkudata(L, 1, RCBN_QUATERNION_METATABLE);
    Quaternion* b = (Quaternion*)luaL_checkudata(L, 2, RCBN_QUATERNION_METATABLE);
    float t = (float)luaL_checknumber(L, 3);
    pushQuaternion(L, Quaternion::Slerp(*a, *b, t));
    return 1;
}

int LuauEngine::quat_index(lua_State* L) {
    Quaternion* q = (Quaternion*)luaL_checkudata(L, 1, RCBN_QUATERNION_METATABLE);
    std::string_view key = luaL_checkstring(L, 2);

    if (key == "w") { lua_pushnumber(L, q->w); return 1; }
    if (key == "x") { lua_pushnumber(L, q->x); return 1; }
    if (key == "y") { lua_pushnumber(L, q->y); return 1; }
    if (key == "z") { lua_pushnumber(L, q->z); return 1; }
    if (key == "toEuler") {
        lua_pushcfunction(L, [](lua_State* L) -> int {
            Quaternion* q = (Quaternion*)luaL_checkudata(L, 1, RCBN_QUATERNION_METATABLE);
            pushVector3(L, q->toEuler());
            return 1;
        }, "toEuler");
        return 1;
    }
    return 0;
}

int LuauEngine::quat_newindex(lua_State* L) {
    Quaternion* q = (Quaternion*)luaL_checkudata(L, 1, RCBN_QUATERNION_METATABLE);
    std::string_view key = luaL_checkstring(L, 2);
    float value = (float)luaL_checknumber(L, 3);

    if (key == "w") q->w = value;
    else if (key == "x") q->x = value;
    else if (key == "y") q->y = value;
    else if (key == "z") q->z = value;
    return 0;
}

int LuauEngine::quat_tostring(lua_State* L) {
    Quaternion* q = (Quaternion*)luaL_checkudata(L, 1, RCBN_QUATERNION_METATABLE);
    std::string str = "Quaternion(" + std::to_string(q->w) + ", " + std::to_string(q->x) + ", " +
                      std::to_string(q->y) + ", " + std::to_string(q->z) + ")";
    lua_pushstring(L, str.c_str());
    return 1;
}

// Quaternion * Quaternion → 合成 / Quaternion * Vector3 → 回転後ベクトル
int LuauEngine::quat_mul(lua_State* L) {
    Quaternion* a = (Quaternion*)luaL_checkudata(L, 1, RCBN_QUATERNION_METATABLE);
    if (void* vd = rcbn_testudata(L, 2, RCBN_VEC3_METATABLE)) {
        pushVector3(L, a->rotate(*(Vector3*)vd));
    } else {
        Quaternion* b = (Quaternion*)luaL_checkudata(L, 2, RCBN_QUATERNION_METATABLE);
        pushQuaternion(L, *a * *b);
    }
    return 1;
}

int LuauEngine::quat_eq(lua_State* L) {
    Quaternion* a = (Quaternion*)luaL_checkudata(L, 1, RCBN_QUATERNION_METATABLE);
    Quaternion* b = (Quaternion*)luaL_checkudata(L, 2, RCBN_QUATERNION_METATABLE);
    lua_pushboolean(L, a->w == b->w && a->x == b->x && a->y == b->y && a->z == b->z);
    return 1;
}

// ==================== CFrame Methods ====================
// CFrame.new() / (x,y,z) / (Vector3 pos) / (Vector3 pos, Quaternion rot)
int LuauEngine::cframe_constructor(lua_State* L) {
    CFrame cf;
    if (lua_isnumber(L, 1)) {
        float x = (float)luaL_checknumber(L, 1);
        float y = (float)luaL_checknumber(L, 2);
        float z = (float)luaL_checknumber(L, 3);
        cf = CFrame(Vector3(x, y, z));
    } else if (void* pd = rcbn_testudata(L, 1, RCBN_VEC3_METATABLE)) {
        Vector3 pos = *(Vector3*)pd;
        if (void* rd = rcbn_testudata(L, 2, RCBN_QUATERNION_METATABLE)) {
            cf = CFrame(pos, *(Quaternion*)rd);
        } else {
            cf = CFrame(pos);
        }
    }
    pushCFrame(L, cf);
    return 1;
}

// CFrame.fromAxisAngle(Vector3 axis, number angleDegrees)
int LuauEngine::cframe_from_axis_angle(lua_State* L) {
    Vector3* axis = (Vector3*)luaL_checkudata(L, 1, RCBN_VEC3_METATABLE);
    float angle = (float)luaL_checknumber(L, 2);
    pushCFrame(L, CFrame::fromAxisAngle(*axis, angle));
    return 1;
}

int LuauEngine::cframe_index(lua_State* L) {
    CFrame* cf = (CFrame*)luaL_checkudata(L, 1, RCBN_CFRAME_METATABLE);
    std::string_view key = luaL_checkstring(L, 2);

    if (key == "Position") { pushVector3(L, cf->Position); return 1; }
    if (key == "Rotation") { pushQuaternion(L, cf->Rotation); return 1; }
    if (key == "inverse") {
        lua_pushcfunction(L, [](lua_State* L) -> int {
            CFrame* cf = (CFrame*)luaL_checkudata(L, 1, RCBN_CFRAME_METATABLE);
            pushCFrame(L, cf->inverse());
            return 1;
        }, "inverse");
        return 1;
    }
    return 0;
}

int LuauEngine::cframe_newindex(lua_State* L) {
    CFrame* cf = (CFrame*)luaL_checkudata(L, 1, RCBN_CFRAME_METATABLE);
    std::string_view key = luaL_checkstring(L, 2);

    if (key == "Position") {
        Vector3* v = (Vector3*)luaL_checkudata(L, 3, RCBN_VEC3_METATABLE);
        cf->Position = *v;
    } else if (key == "Rotation") {
        Quaternion* q = (Quaternion*)luaL_checkudata(L, 3, RCBN_QUATERNION_METATABLE);
        cf->Rotation = *q;
    }
    return 0;
}

int LuauEngine::cframe_tostring(lua_State* L) {
    CFrame* cf = (CFrame*)luaL_checkudata(L, 1, RCBN_CFRAME_METATABLE);
    std::string str = "CFrame(" + cf->Position.toString() + ")";
    lua_pushstring(L, str.c_str());
    return 1;
}

// CFrame * CFrame → 合成 / CFrame * Vector3 → ワールド点
int LuauEngine::cframe_mul(lua_State* L) {
    CFrame* a = (CFrame*)luaL_checkudata(L, 1, RCBN_CFRAME_METATABLE);
    if (void* vd = rcbn_testudata(L, 2, RCBN_VEC3_METATABLE)) {
        pushVector3(L, a->pointToWorld(*(Vector3*)vd));
    } else {
        CFrame* b = (CFrame*)luaL_checkudata(L, 2, RCBN_CFRAME_METATABLE);
        pushCFrame(L, *a * *b);
    }
    return 1;
}

int LuauEngine::cframe_eq(lua_State* L) {
    CFrame* a = (CFrame*)luaL_checkudata(L, 1, RCBN_CFRAME_METATABLE);
    CFrame* b = (CFrame*)luaL_checkudata(L, 2, RCBN_CFRAME_METATABLE);
    bool eq = a->Position == b->Position &&
              a->Rotation.w == b->Rotation.w && a->Rotation.x == b->Rotation.x &&
              a->Rotation.y == b->Rotation.y && a->Rotation.z == b->Rotation.z;
    lua_pushboolean(L, eq);
    return 1;
}
