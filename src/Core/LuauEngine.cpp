#include "include/Core/LuauEngine.hpp"
#include "include/Instances/Workspace.hpp"

// DispatchTableの定義
std::unordered_map<std::string_view, std::unordered_map<std::string_view, LuauEngine::GetterFunc>> LuauEngine::DispatchTable;
std::unordered_map<std::string_view, std::unordered_map<std::string_view, LuauEngine::SetterFunc>> LuauEngine::SetterTable;
Script* LuauEngine::currentScript = nullptr;

void LuauEngine::InitDispatchTable() {
    DispatchTable["Instance"]["Name"] = [](lua_State* L, Instance* obj) {
        std::cout << "Accessing Name of Instance: " << obj->Name << std::endl;
        lua_pushstring(L, obj->Name.c_str());
        return 1;
    };

    DispatchTable["Instance"]["FindChild"] = [](lua_State* L, Instance* obj) {
        std::cout << "Accessing FindChild method\n";
        // objをuserdataとしてスタックに積む（クロージャのupvalueとして使用）
        Instance** userdata = (Instance**)lua_newuserdata(L, sizeof(Instance*));
        *userdata = obj;
        luaL_getmetatable(L, RCBN_INST_METATABLE);
        lua_setmetatable(L, -2);
        // C関数をクロージャとして作成（1つのupvalue）
        lua_pushcclosure(L, instance_find_child_closure, "FindChild", 1);
        return 1;
    };

    DispatchTable["BaseCube"]["Position"] = [](lua_State* L, Instance* obj) {
        auto cube = static_cast<BaseCube*>(obj);
        lua_pushstring(L, cube->Position.toString().c_str());
        return 1;
    };

    DispatchTable["BaseCube"]["Color"] = [](lua_State* L, Instance* obj) {
        auto cube = static_cast<BaseCube*>(obj);
        lua_pushstring(L, cube->Color.toString().c_str());
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

    SetterTable["BaseCube"]["Position"] = [](lua_State* L, Instance* obj) {
        auto cube = static_cast<BaseCube*>(obj);
        // userdataからVector3を取得してセットするロジックをここに実装
         if (lua_isuserdata(L, 3)) {
            Vector3* newPos = (Vector3*)luaL_checkudata(L, 3, RCBN_VEC3_METATABLE);
            cube->Position = *newPos;
            
            // 物理エンジン側にも位置を反映させる
            if (cube->actor) {
                physx::PxTransform pose = cube->actor->getGlobalPose();
                pose.p = physx::PxVec3(newPos->x, newPos->y, newPos->z);
                cube->actor->setGlobalPose(pose);
            }

            // std::cout << "Setting Position of BaseCube to " << cube->Position.toString() << std::endl;
        } else {
            std::cerr << "Expected a Vector3 userdata for Position\n";
        }
        return 0;
    };

    SetterTable["BaseCube"]["Color"] = [](lua_State* L, Instance* obj) {
        auto cube = static_cast<BaseCube*>(obj);
        // userdataからColor4を取得してセットするロジックをここに実装
        if (lua_isuserdata(L, 3)) {
            Color4* newColor = (Color4*)luaL_checkudata(L, 3, RCBN_COLOR4_METATABLE);
            cube->Color = *newColor;
            // std::cout << "Setting Color of BaseCube to " << cube->Color.toString() << std::endl;
        } else {
            std::cerr << "Expected a Color4 userdata for Color\n";
        }
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

    // グローバル関数を登録
    RegisterGlobalFunctions(L);
}

void LuauEngine::RegisterGlobalFunctions(lua_State* L) {
    // Register Vector3 with new method
    lua_newtable(L);
    lua_pushcfunction(L, vec3_constructor, "new");
    lua_setfield(L, -2, "new");
    lua_setglobal(L, "Vector3");

    // Register Color4 with new method
    lua_newtable(L);
    lua_pushcfunction(L, color4_constructor, "new");
    lua_setfield(L, -2, "new");
    lua_setglobal(L, "Color4");

    // Register custom global functions
    lua_pushcfunction(L, global_add, "add");
    lua_setglobal(L, "add");

    lua_pushcfunction(L, global_print_message, "print_message");
    lua_setglobal(L, "print_message");
    
    lua_pushcfunction(L, wait, "wait");
    lua_setglobal(L, "wait");
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
    // luaL_openlibs(L); // !! <Security issue> !!
    luaopen_base(L); // <- これが原因でGLがバグってる？らしい(Claudeによると)
    luaopen_coroutine(L);
    luaopen_math(L);
    luaopen_string(L);
    luaopen_table(L);
    luaopen_bit32(L);

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
    /*
    NOTE: index array
    L[1] = Instance* (obj)
    L[2] = Property (key)
    L[3] = userdata (value)
    */
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

int LuauEngine::instance_find_child_closure(lua_State* L) {
    // upvalue[1]はクロージャに渡されたself
    Instance** objPtr = (Instance**)lua_touserdata(L, lua_upvalueindex(1));
    Instance* obj = *objPtr;
    // L[1] is 'self' from the colon call, L[2] is the actual parameter
    const char* childName = luaL_checkstring(L, 2);
    
    std::cout << "FindChild called with: " << childName << std::endl;
    Instance* child = obj->getChild(childName);
    
    if (child) {
        Instance** userdata = (Instance**)lua_newuserdata(L, sizeof(Instance*));
        *userdata = child;
        luaL_getmetatable(L, RCBN_INST_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    } else {
        lua_pushnil(L);
        return 1;
    }
}

bool LuauEngine::execute(Script& script) {
    // 既にコルーチンがある場合は再開、なければ新規作成
    if (script.Coroutine == nullptr) {
        script.Coroutine = lua_newthread(L);
        // 新しいスレッドにもグローバル関数を登録
        RegisterGlobalFunctions(script.Coroutine);
    }
    
    lua_State* co = script.Coroutine;
    
    // 初回実行の場合、スクリプトをロード
    // lua_status(): 0=OK, LUA_YIELD=suspended, LUA_ERRERR=error, etc.
    if (lua_status(co) == 0 && lua_gettop(co) == 0) {  // スタックが空なら初回実行
        const std::string& source = script.Source;
        size_t bytecodeSize = 0;
        char* bytecode = luau_compile(source.c_str(), source.length(), nullptr, &bytecodeSize);
        if (!bytecode) return false;

        int status = luau_load(co, ("@" + script.Name).c_str(), bytecode, bytecodeSize, 0);
        free(bytecode);

        if (status != 0) {
            script.Aborted = true; // DO NOT loop on errored script compile!
            std::cerr << "Luau Load Error: " << lua_tostring(co, -1) << "\n";
            lua_pop(co, 1);
            return false;
        }
    }
    
    // currentScript を設定
    currentScript = &script;
    
    // コルーチンを再開
    int nargs = 0;
    int result = lua_resume(co, L, nargs);
    
    // 結果を確認
    if (result == LUA_YIELD) {
        // wait() で停止した - Script の Sleeping フラグは wait() 内で設定済み
        currentScript = nullptr;
        return true;
    } else if (result == 0) {
        // 完了
        script.Sleeping = false;
        script.Completed = true;  // 完了フラグをセット
        script.Coroutine = nullptr;  // コルーチンをクリア
        currentScript = nullptr;
        return true;
    } else {
        // エラー
        script.Aborted = true; // DO NOT loop on errored script!
        std::cerr << "Luau Run Error caught. Status: " << result << "\n";
        if (lua_type(co, -1) == LUA_TSTRING) {
            std::cerr << "Luau Run Error: " << lua_tostring(co, -1) << "\n";
        } else {
            std::cerr << "Luau Run Error (non-string error object)\n";
        }
        lua_pop(co, 1);
        currentScript = nullptr;
        return false;
    }
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

// ==================== Global Functions ====================
int LuauEngine::global_add(lua_State* L) {
    // 2つの数値を取得
    float a = (float)luaL_checknumber(L, 1);
    float b = (float)luaL_checknumber(L, 2);
    
    // 合計を計算してスタックに積む
    lua_pushnumber(L, a + b);
    
    // 戻り値の個数を返す
    return 1;
}

int LuauEngine::wait(lua_State* L) {
    float s = (float)luaL_checknumber(L, 1);
    
    // 現在実行中のスクリプトに待機情報を記録
    if (currentScript != nullptr) {
        currentScript->Sleeping = true;
        currentScript->SleepTime = s;
        currentScript->SleepRemaining = s;
    }
    
    // スクリプト実行を一時停止
    return lua_yield(L, 0);
}

int LuauEngine::global_print_message(lua_State* L) {
    // 文字列引数を取得
    const char* message = luaL_checkstring(L, 1);
    
    // C++で出力
    std::cout << "[Luau] " << message << std::endl;
    
    // 戻り値なし
    return 0;
}

void LuauEngine::setWorkspace(Workspace* ws) {
    workspace = ws;
}

void LuauEngine::executeWorkspaceScripts() {
    if (workspace == nullptr) return;
    
    // Workspace 内のすべてのスクリプトを実行
    for (Instance* inst : workspace->scripts) {
        Script* script = dynamic_cast<Script*>(inst);
        // 条件：有効 && 待機中でない && 完了していない && 中断されていない
        if (script && script->Enabled && !script->Sleeping && !script->Completed && !script->Aborted) {
            execute(*script);
        }
    }
}

void LuauEngine::update(float deltaTime) {
    if (workspace == nullptr) return;
    
    // 待機中のスクリプトのタイマーを減算
    for (Instance* inst : workspace->scripts) {
        Script* script = dynamic_cast<Script*>(inst);
        if (script && script->Sleeping && script->Coroutine != nullptr) {
            script->SleepRemaining -= deltaTime;
            
            // タイムアウト時にコルーチンを再開
            if (script->SleepRemaining <= 0.0f) {
                script->Sleeping = false;
                execute(*script);
            }
        }
    }
}