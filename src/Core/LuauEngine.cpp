#include "include/Core/LuauEngine.hpp"

// DispatchTableの定義
std::unordered_map<std::string_view, std::unordered_map<std::string_view, LuauEngine::GetterFunc>> LuauEngine::DispatchTable;
std::unordered_map<std::string_view, std::unordered_map<std::string_view, LuauEngine::SetterFunc>> LuauEngine::SetterTable;

void LuauEngine::InitDispatchTable() {
    DispatchTable["Instance"]["Name"] = [](lua_State* L, Instance* obj) {
        std::cout << "Accessing Name of Instance: " << obj->Name << std::endl;
        lua_pushstring(L, obj->Name.c_str());
        return 1;
    };

    DispatchTable["BaseCube"]["Position"] = [](lua_State* L, Instance* obj) {
        auto cube = static_cast<BaseCube*>(obj);
        lua_pushstring(L, cube->Position.toString().c_str());
        return 1;
    };
}

void LuauEngine::InitSetterTable() {
    SetterTable["Instance"]["Name"] = [](lua_State* L, Instance* obj) {
        const char* newName = luaL_checkstring(L, 3);
        std::cout << "Setting Name of Instance from " << obj->Name << " to " << newName << std::endl;
        obj->Name = newName;
        return 0;
    };
}

void LuauEngine::InitMetatables() {
    // Instance metatable
    luaL_newmetatable(L, RCBN_INST_METATABLE);
    lua_pushcfunction(L, instance_index, "instance_index");
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, instance_newindex, "instance_newindex");
    lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, instance_tostring, "instance_tostring");
    lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);

    // Vector3 metatable
    luaL_newmetatable(L, RCBN_VEC3_METATABLE);
    lua_pushcfunction(L, vec3_index, "vec3_index");
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, vec3_newindex, "vec3_newindex");
    lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, vec3_tostring, "vec3_tostring");
    lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);

    // Color4 metatable
    luaL_newmetatable(L, RCBN_COLOR4_METATABLE);
    lua_pushcfunction(L, color4_index, "color4_index");
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, color4_newindex, "color4_newindex");
    lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, color4_tostring, "color4_tostring");
    lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);

    // Register constructors as global functions
    lua_pushcfunction(L, vec3_constructor, "Vector3");
    lua_setglobal(L, "Vector3");

    lua_pushcfunction(L, color4_constructor, "Color4");
    lua_setglobal(L, "Color4");
}

int LuauEngine::instance_index(lua_State* L) {
    Instance* obj = *(Instance**)luaL_checkudata(L, 1, RCBN_INST_METATABLE);
    std::string_view key = luaL_checkstring(L, 2);

    for (const auto& [className, classProps] : DispatchTable) {
        std::cout << "Checking class: " << className << " for property: " << key << std::endl;
        if (obj->IsA(std::string(className))) {
            std::cout << "Found class: " << className << " for object: " << obj->GetClassName() << std::endl;
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

    InitMetatables();
    InitDispatchTable();
    InitSetterTable();
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

int LuauEngine::instance_newindex(lua_State* L) {
    Instance* obj = *(Instance**)luaL_checkudata(L, 1, RCBN_INST_METATABLE);
    std::string_view key = luaL_checkstring(L, 2);

    for (const auto& [className, classProps] : SetterTable) {
        if (obj->IsA(std::string(className))) {
            if (auto it = classProps.find(key); it != classProps.end()) {
                auto& [name, setProperty] = *it;
                return setProperty(L, obj);
            }
        }
    }

    return 0;
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