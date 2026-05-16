#include "include/Core/LuauEngine.hpp"
#include "include/Core/Physics.hpp"
#include "include/Instances/Workspace.hpp"
#include "include/Instances/Sound.hpp"
#include "include/Util/Logger.hpp"
#include <float.h>

// DispatchTableの定義
std::unordered_map<std::string_view, std::unordered_map<std::string_view, LuauEngine::GetterFunc>> LuauEngine::DispatchTable;
std::unordered_map<std::string_view, std::unordered_map<std::string_view, LuauEngine::SetterFunc>> LuauEngine::SetterTable;
Script* LuauEngine::currentScript = nullptr;

void restoreFPU() {
    unsigned int control;
    _controlfp_s(&control, 0, 0); // 現在の状態取得
    // 標準的な安全設定に戻す（double精度、例外マスク全on）
    _controlfp_s(NULL, _PC_53 | _RC_NEAR | _MCW_EM, 
                       _MCW_PC | _MCW_RC | _MCW_EM);
}

void LuauEngine::InitDispatchTable() {
    InitDispatchTable_Base();
    InitDispatchTable_World();
    InitDispatchTable_Physics();
    InitDispatchTable_Misc();
}

void LuauEngine::InitSetterTable() {
    InitSetterTable_Base();
    InitSetterTable_World();
    InitSetterTable_Physics();
    InitSetterTable_Misc();
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
    lua_pushcfunction(L, [](lua_State* L) -> int {
        auto* userdata = (std::weak_ptr<Instance>*)luaL_checkudata(L, 1, RCBN_INST_METATABLE);
        userdata->~weak_ptr();
        return 0;
    }, "__gc");
    lua_setfield(L, -2, "__gc");
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

    // The legend who built something nice
    luaL_newmetatable(L, ERIK);
    lua_pushcfunction(L, erik_index, "erik_index");
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, erik_tostring, "erik_tostring");
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
    Vector3* zeroVec = (Vector3*)lua_newuserdata(L, sizeof(Vector3));
    *zeroVec = Vector3(0.0f, 0.0f, 0.0f);
    luaL_getmetatable(L, RCBN_VEC3_METATABLE);
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, "zero");
    lua_setglobal(L, "Vector3");

    // Register Color4 with new method
    lua_newtable(L);
    lua_pushcfunction(L, color4_constructor, "new");
    lua_setfield(L, -2, "new");
    lua_setglobal(L, "Color4");

    lua_newtable(L);
    luaL_getmetatable(L, ERIK);
    lua_setmetatable(L, -2);
    lua_setglobal(L, ERIK);

    // Register custom global functions
    lua_pushcfunction(L, global_add, "add");
    lua_setglobal(L, "add");

    lua_pushcfunction(L, global_print_message, "print");
    lua_setglobal(L, "print");
    
    lua_pushcfunction(L, wait, "wait");
    lua_setglobal(L, "wait");

    auto ws = workspace.lock();
    if (ws) {
        auto* ud = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
        new (ud) std::weak_ptr<Instance>(ws);
        luaL_getmetatable(L, RCBN_INST_METATABLE);
        lua_setmetatable(L, -2);
        lua_setglobal(L, "workspace");
    }
}

int LuauEngine::instance_index(lua_State* L) {
    auto* userdata = (std::weak_ptr<Instance>*)luaL_checkudata(L, 1, RCBN_INST_METATABLE);
    auto obj_shared = userdata->lock();
    if (!obj_shared) {
        lua_pushnil(L);
        return 1;
    }
    Instance* obj = obj_shared.get();
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
    // luaL_openlibs(L); // !! <Security issue> !!
    luaopen_base(L);
    luaopen_coroutine(L);
    luaopen_math(L);
    luaopen_string(L);
    luaopen_table(L);
    luaopen_bit32(L);

    lua_callbacks(L)->userdata = this;

    // エラー捕捉時（スタック巻き戻し前）にトレースバックを保存
    lua_callbacks(L)->debugprotectederror = [](lua_State* L) {
        const char* trace = lua_debugtrace(L);
        auto* engine = static_cast<LuauEngine*>(lua_callbacks(L)->userdata);
        if (engine) engine->m_lastTraceback = trace ? trace : "";
    };

    // Luauパニック時にプロセスがクラッシュするのを防ぐ
    lua_callbacks(L)->panic = [](lua_State* L, int) {
        const char* raw = lua_tostring(L, -1);
        std::string msg = raw ? raw : "unknown panic";
        std::cerr << "[LUAU PANIC] " << msg << "\n";
        if (g_luauLogHook) g_luauLogHook("[ERROR] [PANIC] " + msg);
    };

    InitMetatables();
    InitDispatchTable();
    InitSetterTable();
}

LuauEngine::~LuauEngine() {
    if (L) lua_close(L);
}

void LuauEngine::setBindings(const std::shared_ptr<Instance>& instance) {
    auto* userdata = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
    new (userdata) std::weak_ptr<Instance>(instance);

    luaL_getmetatable(L, RCBN_INST_METATABLE);
    lua_setmetatable(L, -2);
}

void LuauEngine::setGlobalInstance(const std::string& name, const std::shared_ptr<Instance>& instance) {
    setBindings(instance);
    lua_setglobal(L, name.c_str());
}

int LuauEngine::instance_newindex(lua_State* L) {
    auto* userdata = (std::weak_ptr<Instance>*)luaL_checkudata(L, 1, RCBN_INST_METATABLE);
    auto obj_shared = userdata->lock();
    if (!obj_shared) return 0;
    Instance* obj = obj_shared.get();
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
    auto* userdata = (std::weak_ptr<Instance>*)luaL_checkudata(L, 1, RCBN_INST_METATABLE);
    auto obj_shared = userdata->lock();
    if (!obj_shared) {
        lua_pushstring(L, "Instance: (Deleted)");
        return 1;
    }
    std::string str = "Instance: " + obj_shared->Name;
    lua_pushstring(L, str.c_str());
    return 1;
}

int LuauEngine::instance_find_child_closure(lua_State* L) {
    // upvalue[1]はクロージャに渡されたself
    auto* userdata = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto obj_shared = userdata->lock();
    if (!obj_shared) return 0;
    Instance* obj = obj_shared.get();
    // L[1] is 'self' from the colon call, L[2] is the actual parameter
    const char* childName = luaL_checkstring(L, 2);
    
    std::cout << "FindChild called with: " << childName << std::endl;
    Instance* child = obj->getChild(childName);
    
    if (child) {
        auto* userdata = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
        new (userdata) std::weak_ptr<Instance>(child->shared_from_this());
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
        RegisterGlobalFunctions(script.Coroutine);
        auto* sud = (std::weak_ptr<Instance>*)lua_newuserdata(script.Coroutine, sizeof(std::weak_ptr<Instance>));
        new (sud) std::weak_ptr<Instance>(script.shared_from_this());
        luaL_getmetatable(script.Coroutine, RCBN_INST_METATABLE);
        lua_setmetatable(script.Coroutine, -2);
        lua_setglobal(script.Coroutine, "script");
    }
    
    lua_State* co = script.Coroutine;
    
    // 初回実行の場合、スクリプトをロード
    // lua_status(): 0=OK, LUA_YIELD=suspended, LUA_ERRERR=error, etc.
    if (lua_status(co) == 0 && lua_gettop(co) == 0) {  // スタックが空なら初回実行
        const std::string& source = script.Source;
        int status;
        if (script.isPrecompiled) {
            // .luauc: source already contains raw bytecode, pass directly
            status = luau_load(co, ("@" + script.Name).c_str(),
                               source.data(), source.size(), 0);
        } else {
            size_t bytecodeSize = 0;
            char* bytecode = luau_compile(source.c_str(), source.length(), nullptr, &bytecodeSize);
            if (!bytecode) return false;
            status = luau_load(co, ("@" + script.Name).c_str(), bytecode, bytecodeSize, 0);
            free(bytecode);
        }

        if (status != 0) {
            script.Aborted = true; // DO NOT loop on errored script compile!
            const char* raw = (lua_gettop(co) > 0) ? lua_tostring(co, -1) : nullptr;
            const std::string errMsg = raw ? raw : "compile error";
            std::cerr << "Luau Load Error: " << errMsg << "\n";
            if (g_luauLogHook) g_luauLogHook("[ERROR] " + errMsg);
            if (lua_gettop(co) > 0) lua_pop(co, 1);
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
        // Luauはエラーメッセージをcoではなく親スレッドLに積む場合がある
        lua_State* errState = (lua_gettop(co) > 0) ? co : L;
        std::string errMsg = "unknown error";
        if (lua_gettop(errState) > 0 && lua_type(errState, -1) == LUA_TSTRING) {
            const char* raw = lua_tostring(errState, -1);
            if (raw) errMsg = raw;
            lua_pop(errState, 1);
        } else if (lua_gettop(errState) > 0) {
            lua_pop(errState, 1);
        }

        // debugprotectederror で取得したスタックトレースを使う
        const std::string output = m_lastTraceback.empty() ? errMsg : m_lastTraceback;
        m_lastTraceback.clear();
        std::cerr << "Luau Run Error: " << output << "\n";
        if (g_luauLogHook) g_luauLogHook("[ERROR] " + output);
        currentScript = nullptr;
        restoreFPU(); // エラー後にFPU状態が乱れる可能性があるため、復元する
        return false;
    }
}

// ==================== Workspace Methods ====================
int LuauEngine::workspace_raycast_closure(lua_State* L) {
    auto* ptr = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto ws_shared = ptr->lock();
    if (!ws_shared) return 0;
    Workspace* ws = static_cast<Workspace*>(ws_shared.get());

    // L[1] = self, L[2] = origin, L[3] = direction, L[4] = params (ignored)
    Vector3* origin    = (Vector3*)luaL_checkudata(L, 2, RCBN_VEC3_METATABLE);
    Vector3* direction = (Vector3*)luaL_checkudata(L, 3, RCBN_VEC3_METATABLE);

    Physics* physics = ws->getPhysicsEngine();
    if (!physics) {
        lua_pushnil(L);
        return 1;
    }

    RaycastHit hit;
    // NOTE: 最大距離が1000ユニットなので拡大は要検討
    bool didHit = physics->raycast(*origin, *direction, 1000.0f, hit);

    if (!didHit || !hit.hit) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);

    lua_pushnumber(L, hit.distance);
    lua_setfield(L, -2, "Distance");

    Vector3* pos = (Vector3*)lua_newuserdata(L, sizeof(Vector3));
    *pos = hit.position;
    luaL_getmetatable(L, RCBN_VEC3_METATABLE);
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, "Position");

    if (hit.instance) {
        auto* userdata = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
        new (userdata) std::weak_ptr<Instance>(hit.instance->shared_from_this());
        luaL_getmetatable(L, RCBN_INST_METATABLE);
        lua_setmetatable(L, -2);
        lua_setfield(L, -2, "Instance");
    } else {
        lua_pushnil(L);
        lua_setfield(L, -2, "Instance");
    }

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
    int n = lua_gettop(L);
    std::ostringstream ss;
    for (int i = 1; i <= n; i++) {
        if (i > 1) ss << "\t";
        ss << luaL_tolstring(L, i, nullptr);
        lua_pop(L, 1);
    }
    const std::string msg = ss.str();
    std::cout << "[Luau] " << msg << std::endl;
    if (g_luauLogHook) g_luauLogHook(msg);
    return 0;
}

int LuauEngine::erik_tostring(lua_State* L) {
    lua_pushstring(L, "What is the most important part of a sandwich?");
    return 1;
}

int LuauEngine::erik_index(lua_State* L) {
    // L, 1 is table (maybe self)
    std::string_view key = luaL_checkstring(L, 2);
    RCBN_LOG(key);
    if (key == "cassel") {
        lua_pushstring(L, "Who you share it with.");
        return 1;
    }
    return 0;
}

void LuauEngine::setWorkspace(const std::shared_ptr<Workspace>& ws) {
    workspace = ws;
    setGlobalInstance("workspace", ws);
}

void LuauEngine::executeWorkspaceScripts() {
    auto ws = workspace.lock();
    if (!ws) return;
    
    // Workspace 内のすべてのスクリプトを実行
    for (auto& inst : ws->scripts) {
        auto script = std::dynamic_pointer_cast<Script>(inst);
        // 条件：有効 && 待機中でない && 完了していない && 中断されていない
        if (script && script->Enabled && !script->Sleeping && !script->Completed && !script->Aborted) {
            execute(*script);
        }
    }
}

int LuauEngine::instance_get_children_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto obj = ud->lock();
    lua_newtable(L);
    if (!obj) return 1;
    int idx = 1;
    for (auto& [name, child] : obj->children) {
        auto* cud = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
        new (cud) std::weak_ptr<Instance>(child);
        luaL_getmetatable(L, RCBN_INST_METATABLE);
        lua_setmetatable(L, -2);
        lua_rawseti(L, -2, idx++);
    }
    return 1;
}

int LuauEngine::instance_is_a_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto obj = ud->lock();
    if (!obj) { lua_pushboolean(L, false); return 1; }
    const char* className = luaL_checkstring(L, 2);
    lua_pushboolean(L, obj->IsA(className));
    return 1;
}

int LuauEngine::instance_destroy_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto obj = ud->lock();
    if (!obj) return 0;
    auto parent = obj->Parent.lock();
    if (parent) {
        parent->children.erase(obj->Name);
        obj->Parent.reset();
        obj->onAncestorChanged();
    }
    return 0;
}

int LuauEngine::sound_play_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto obj = ud->lock();
    if (obj) static_cast<Sound*>(obj.get())->play();
    return 0;
}

int LuauEngine::sound_stop_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto obj = ud->lock();
    if (obj) static_cast<Sound*>(obj.get())->stop();
    return 0;
}

void LuauEngine::update(float deltaTime) {
    auto ws = workspace.lock();
    if (!ws) return;
    
    // 待機中のスクリプトのタイマーを減算
    for (auto& inst : ws->scripts) {
        auto script = std::dynamic_pointer_cast<Script>(inst);
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