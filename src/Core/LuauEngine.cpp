#include "include/Core/LuauEngine.hpp"
#include "include/Core/LuarCompiler.hpp"
#include "include/Core/Physics.hpp"
#include "include/Core/RCBNScriptSignal.hpp"
#include "include/Instances/Workspace.hpp"
#include "include/Instances/Sound.hpp"
#include "include/Instances/System.hpp"
#include "include/Instances/Event.hpp"
#include "include/Instances/TextLabel.hpp"
#include "include/Instances/TextButton.hpp"
#include "include/Instances/SurfaceGui.hpp"
#include "include/Instances/BillboardGui.hpp"
#include "include/Instances/ProximityPrompt.hpp"
#include "include/Core/TerrainStreamer.hpp"
#include "include/Util/Color4.hpp"
#include "include/Util/Logger.hpp"
#include <cmath>
#include <algorithm>
#include <float.h>
#include <fenv.h>
#include <xmmintrin.h>

// DispatchTableの定義
std::unordered_map<std::string_view, std::unordered_map<std::string_view, LuauEngine::GetterFunc>> LuauEngine::DispatchTable;
std::unordered_map<std::string_view, std::unordered_map<std::string_view, LuauEngine::SetterFunc>> LuauEngine::SetterTable;
Script* LuauEngine::currentScript = nullptr;

// Instance.new で生成したインスタンスの所有権を保持するストレージ
static std::vector<std::shared_ptr<Instance>> s_ownedInstances;

static std::shared_ptr<Workspace> getScriptWorkspace(Script& script) {
    Instance* ws = script.findFirstAncestorWorkspace();
    if (!ws) return nullptr;
    return std::static_pointer_cast<Workspace>(ws->shared_from_this());
}

static void setWorkspaceGlobal(lua_State* state, const std::shared_ptr<Workspace>& ws) {
    if (!ws) {
        lua_pushnil(state);
        lua_setglobal(state, "workspace");
        return;
    }

    auto* ud = (std::weak_ptr<Instance>*)lua_newuserdata(state, sizeof(std::weak_ptr<Instance>));
    new (ud) std::weak_ptr<Instance>(ws);
    luaL_getmetatable(state, LuauEngine::RCBN_INST_METATABLE);
    lua_setmetatable(state, -2);
    lua_setglobal(state, "workspace");
}

struct FPUState {
    fenv_t env;
    unsigned int mxcsr;
};

FPUState saveFPU() {
    FPUState s;
    fegetenv(&s.env);
    s.mxcsr = _mm_getcsr();
    return s;
}

void restoreFPU(const FPUState& s) {
    fesetenv(&s.env);
    _mm_setcsr(s.mxcsr);
}

void LuauEngine::InitDispatchTable() {
    InitDispatchTable_Base();
    InitDispatchTable_World();
    InitDispatchTable_Physics();
    InitDispatchTable_Misc();
    InitDispatchTable_GUI();
}

void LuauEngine::InitSetterTable() {
    InitSetterTable_Base();
    InitSetterTable_World();
    InitSetterTable_Physics();
    InitSetterTable_Misc();
    InitSetterTable_GUI();
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
    lua_pushcfunction(L, vec3_add, "vec3_add");
    lua_setfield(L, -2, "__add");
    lua_pushcfunction(L, vec3_sub, "vec3_sub");
    lua_setfield(L, -2, "__sub");
    lua_pushcfunction(L, vec3_unm, "vec3_unm");
    lua_setfield(L, -2, "__unm");
    lua_pushcfunction(L, vec3_mul, "vec3_mul");
    lua_setfield(L, -2, "__mul");
    lua_pushcfunction(L, vec3_div, "vec3_div");
    lua_setfield(L, -2, "__div");
    lua_pushcfunction(L, vec3_eq, "vec3_eq");
    lua_setfield(L, -2, "__eq");
    lua_pop(L, 1);

    // Color4 metatable
    luaL_newmetatable(L, RCBN_COLOR4_METATABLE);
    lua_pushcfunction(L, color4_index, "color4_index");
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, color4_newindex, "color4_newindex");
    lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, color4_tostring, "color4_tostring");
    lua_setfield(L, -2, "__tostring");
    lua_pushcfunction(L, color4_add, "color4_add");
    lua_setfield(L, -2, "__add");
    lua_pushcfunction(L, color4_sub, "color4_sub");
    lua_setfield(L, -2, "__sub");
    lua_pushcfunction(L, color4_unm, "color4_unm");
    lua_setfield(L, -2, "__unm");
    lua_pushcfunction(L, color4_mul, "color4_mul");
    lua_setfield(L, -2, "__mul");
    lua_pushcfunction(L, color4_div, "color4_div");
    lua_setfield(L, -2, "__div");
    lua_pushcfunction(L, color4_eq, "color4_eq");
    lua_setfield(L, -2, "__eq");
    lua_pop(L, 1);

    // The legend who built something nice
    luaL_newmetatable(L, ERIK);
    lua_pushcfunction(L, erik_index, "erik_index");
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, erik_tostring, "erik_tostring");
    lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);

    // Signal metatable
    luaL_newmetatable(L, RCBN_SIGNAL_METATABLE);
    lua_pushcfunction(L, signal_index, "signal_index");
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, [](lua_State* Lx) -> int {
        auto* ud = (std::shared_ptr<RCBNScriptSignal>*)lua_touserdata(Lx, 1);
        ud->~shared_ptr();
        return 0;
    }, "__gc");
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);

    // Connection metatable
    luaL_newmetatable(L, RCBN_CONNECTION_METATABLE);
    lua_pushcfunction(L, connection_index, "connection_index");
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, [](lua_State* Lx) -> int {
        auto* ud = (std::shared_ptr<RCBNScriptConnection>*)lua_touserdata(Lx, 1);
        ud->~shared_ptr();
        return 0;
    }, "__gc");
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);

    // Vector2 metatable
    luaL_newmetatable(L, RCBN_VEC2_METATABLE);
    lua_pushcfunction(L, vec2_index,    "vec2_index");    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, vec2_newindex, "vec2_newindex"); lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, vec2_tostring, "vec2_tostring"); lua_setfield(L, -2, "__tostring");
    lua_pushcfunction(L, vec2_add, "vec2_add"); lua_setfield(L, -2, "__add");
    lua_pushcfunction(L, vec2_sub, "vec2_sub"); lua_setfield(L, -2, "__sub");
    lua_pushcfunction(L, vec2_unm, "vec2_unm"); lua_setfield(L, -2, "__unm");
    lua_pushcfunction(L, vec2_mul, "vec2_mul"); lua_setfield(L, -2, "__mul");
    lua_pushcfunction(L, vec2_div, "vec2_div"); lua_setfield(L, -2, "__div");
    lua_pushcfunction(L, vec2_eq,  "vec2_eq");  lua_setfield(L, -2, "__eq");
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

    // Register Vector2 with new method
    lua_newtable(L);
    lua_pushcfunction(L, vec2_constructor, "new");
    lua_setfield(L, -2, "new");
    lua_setglobal(L, "Vector2");

    lua_newtable(L);
    luaL_getmetatable(L, ERIK);
    lua_setmetatable(L, -2);
    lua_setglobal(L, ERIK);

    // Instance.new
    lua_newtable(L);
    lua_pushcfunction(L, instance_new_closure, "new");
    lua_setfield(L, -2, "new");
    lua_setglobal(L, "Instance");

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

        // コルーチンにもデバッグコールバックを伝播させる
        lua_callbacks(script.Coroutine)->userdata = lua_callbacks(L)->userdata;
        lua_callbacks(script.Coroutine)->debugprotectederror = [](lua_State* Lco) {
            const char* trace = lua_debugtrace(Lco);
            auto* engine = static_cast<LuauEngine*>(lua_callbacks(Lco)->userdata);
            if (engine) engine->m_lastTraceback = trace ? trace : "";
        };

        RegisterGlobalFunctions(script.Coroutine);
        auto* sud = (std::weak_ptr<Instance>*)lua_newuserdata(script.Coroutine, sizeof(std::weak_ptr<Instance>));
        new (sud) std::weak_ptr<Instance>(script.shared_from_this());
        luaL_getmetatable(script.Coroutine, RCBN_INST_METATABLE);
        lua_setmetatable(script.Coroutine, -2);
        lua_setglobal(script.Coroutine, "script");
    }
    
    lua_State* co = script.Coroutine;
    setWorkspaceGlobal(co, getScriptWorkspace(script));
    
    // 初回実行の場合、スクリプトをロード
    // lua_status(): 0=OK, LUA_YIELD=suspended, LUA_ERRERR=error, etc.
    if (lua_status(co) == 0 && lua_gettop(co) == 0) {  // スタックが空なら初回実行
        std::string compiledSource;
        const std::string* sourcePtr = &script.Source;

        // .luarファイルはRust製LuarコンパイラでLuauに変換する
        auto endsWithLuar = [](const std::string& s) {
            return s.size() >= 5 && s.substr(s.size() - 5) == ".luar";
        };
        bool isLuar = endsWithLuar(script.Name) || endsWithLuar(script.Path);
        if (isLuar) {
            static LuarCompiler s_luarCompiler;
            compiledSource = s_luarCompiler.compile(script.Source);
            if (compiledSource.empty()) { script.Aborted = true; return false; }
            RCBN_LOG("\033[32m Compiling Luar Source has succeeded!\033[0m");
            // std::cerr << "[LuarCompiler] Output:\n" << compiledSource << "\n---\n";
            sourcePtr = &compiledSource;
        }

        const std::string& source = *sourcePtr;
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
    FPUState fpuState = saveFPU();
    int result = lua_resume(co, L, nargs);
    restoreFPU(fpuState);

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
        if (lua_gettop(errState) > 0) {
            const char* raw = luaL_tolstring(errState, -1, nullptr);
            if (raw) errMsg = raw;
            lua_pop(errState, 2); // luaL_tolstring が文字列を積むので2つポップ
        }

        // debugprotectederror で取得したスタックトレースを使う
        const std::string output = m_lastTraceback.empty() ? errMsg : m_lastTraceback;
        m_lastTraceback.clear();
        std::cerr << "Luau Run Error: " << output << "\n";
        if (g_luauLogHook) g_luauLogHook("[ERROR] " + output);
        currentScript = nullptr;
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

// ==================== Terrain methods ====================
// 共通: upvalue1 の Terrain を取り出し、streamer が有効なら返す。無効なら nullptr。
static TerrainStreamer* getTerrainStreamerFromUpvalue(lua_State* L) {
    auto* ptr = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    if (!ptr) return nullptr;
    auto shared = ptr->lock();
    if (!shared || !shared->IsA("Terrain")) return nullptr;
    Terrain* terrain = static_cast<Terrain*>(shared.get());
    if (!terrain->Enabled || !terrain->streamer) return nullptr;
    return terrain->streamer.get();
}

// ワールド座標(studs) → ブロックインデックス。ブロック中心は index*BS にある。
static inline int32_t studToBlock(float v) {
    return (int32_t)std::floor(v / TerrainStreamer::BLOCK_STUD_SIZE + 0.5f);
}

static inline uint8_t colorChannelToByte(float c) {
    int v = (int)std::lround(c * 255.0f);
    return (uint8_t)std::clamp(v, 0, 255);
}

int LuauEngine::terrain_set_block_closure(lua_State* L) {
    TerrainStreamer* streamer = getTerrainStreamerFromUpvalue(L);
    if (!streamer) { lua_pushboolean(L, false); return 1; }

    // L[1]=self, L[2]=position(Vector3), L[3]=color(Color4, 省略可), L[4]=shape(number, 省略可)
    Vector3* pos = (Vector3*)luaL_checkudata(L, 2, RCBN_VEC3_METATABLE);

    uint8_t r = 200, g = 200, b = 200; // デフォルト色（明るいグレー）
    if (!lua_isnoneornil(L, 3)) {
        Color4* col = (Color4*)luaL_checkudata(L, 3, RCBN_COLOR4_METATABLE);
        r = colorChannelToByte(col->r);
        g = colorChannelToByte(col->g);
        b = colorChannelToByte(col->b);
    }

    BlockShape shape = BlockShape::Cube;
    if (!lua_isnoneornil(L, 4)) {
        int s = (int)luaL_checkinteger(L, 4);
        if (s >= 0 && s <= (int)BlockShape::Ramp_W) shape = (BlockShape)s;
    }

    bool ok = streamer->setBlock(studToBlock(pos->x), studToBlock(pos->y), studToBlock(pos->z),
                                 shape, r, g, b);
    lua_pushboolean(L, ok);
    return 1;
}

int LuauEngine::terrain_remove_block_closure(lua_State* L) {
    TerrainStreamer* streamer = getTerrainStreamerFromUpvalue(L);
    if (!streamer) { lua_pushboolean(L, false); return 1; }

    Vector3* pos = (Vector3*)luaL_checkudata(L, 2, RCBN_VEC3_METATABLE);
    bool ok = streamer->removeBlock(studToBlock(pos->x), studToBlock(pos->y), studToBlock(pos->z));
    lua_pushboolean(L, ok);
    return 1;
}

int LuauEngine::terrain_get_block_closure(lua_State* L) {
    TerrainStreamer* streamer = getTerrainStreamerFromUpvalue(L);
    if (!streamer) { lua_pushnil(L); return 1; }

    Vector3* pos = (Vector3*)luaL_checkudata(L, 2, RCBN_VEC3_METATABLE);
    const Block* blk = streamer->getBlockGlobal(studToBlock(pos->x), studToBlock(pos->y), studToBlock(pos->z));
    if (!blk || blk->isEmpty()) { lua_pushnil(L); return 1; }

    lua_newtable(L);
    lua_pushinteger(L, (int)blk->shape);
    lua_setfield(L, -2, "Shape");

    Color4* col = (Color4*)lua_newuserdata(L, sizeof(Color4));
    *col = Color4(blk->r / 255.0f, blk->g / 255.0f, blk->b / 255.0f, 1.0f);
    luaL_getmetatable(L, RCBN_COLOR4_METATABLE);
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, "Color");
    return 1;
}

int LuauEngine::terrain_raycast_closure(lua_State* L) {
    TerrainStreamer* streamer = getTerrainStreamerFromUpvalue(L);
    if (!streamer) { lua_pushnil(L); return 1; }

    // L[1]=self, L[2]=origin, L[3]=direction, L[4]=maxDist(省略可)
    Vector3* origin = (Vector3*)luaL_checkudata(L, 2, RCBN_VEC3_METATABLE);
    Vector3* dir    = (Vector3*)luaL_checkudata(L, 3, RCBN_VEC3_METATABLE);
    float maxDist = lua_isnoneornil(L, 4) ? 1000.0f : (float)luaL_checknumber(L, 4);

    Vector3 hitPos;
    if (!streamer->raycastVoxel(*origin, dir->normalize(), maxDist, hitPos)) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    Vector3* p = (Vector3*)lua_newuserdata(L, sizeof(Vector3));
    *p = hitPos;
    luaL_getmetatable(L, RCBN_VEC3_METATABLE);
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, "Position");
    return 1;
}

int LuauEngine::terrain_apply_brush_closure(lua_State* L) {
    TerrainStreamer* streamer = getTerrainStreamerFromUpvalue(L);
    if (!streamer) return 0;

    // L[1]=self, L[2]=position, L[3]=radius, L[4]=mode(+1/-1/0)
    Vector3* pos = (Vector3*)luaL_checkudata(L, 2, RCBN_VEC3_METATABLE);
    float radius = (float)luaL_checknumber(L, 3);
    int   mode   = (int)luaL_checkinteger(L, 4);
    streamer->applyBrush(*pos, radius, mode);
    return 0;
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

void LuauEngine::executeWorkspaceScripts(Workspace& ws) {
    // このWorkspaceのスクリプトが実行される前に workspace グローバルを切り替える
    auto wsSp = std::static_pointer_cast<Workspace>(ws.shared_from_this());
    auto* ud = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
    new (ud) std::weak_ptr<Instance>(wsSp);
    luaL_getmetatable(L, RCBN_INST_METATABLE);
    lua_setmetatable(L, -2);
    lua_setglobal(L, "workspace");

    for (auto& inst : ws.scripts) {
        auto script = std::dynamic_pointer_cast<Script>(inst);
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

// ===================================================
//  Signal ヘルパー
// ===================================================
void LuauEngine::pushSignal(lua_State* Lx, std::shared_ptr<RCBNScriptSignal> sig) {
    auto* ud = (std::shared_ptr<RCBNScriptSignal>*)lua_newuserdata(Lx, sizeof(std::shared_ptr<RCBNScriptSignal>));
    new (ud) std::shared_ptr<RCBNScriptSignal>(std::move(sig));
    luaL_getmetatable(Lx, RCBN_SIGNAL_METATABLE);
    lua_setmetatable(Lx, -2);
}

void LuauEngine::pushConnection(lua_State* Lx, std::shared_ptr<RCBNScriptConnection> conn) {
    auto* ud = (std::shared_ptr<RCBNScriptConnection>*)lua_newuserdata(Lx, sizeof(std::shared_ptr<RCBNScriptConnection>));
    new (ud) std::shared_ptr<RCBNScriptConnection>(std::move(conn));
    luaL_getmetatable(Lx, RCBN_CONNECTION_METATABLE);
    lua_setmetatable(Lx, -2);
}

void LuauEngine::pushVector2(lua_State* L, Vector2 v) {
    auto* ud = (Vector2*)lua_newuserdata(L, sizeof(Vector2));
    *ud = v;
    luaL_getmetatable(L, RCBN_VEC2_METATABLE);
    lua_setmetatable(L, -2);
}

void LuauEngine::pushVector3(lua_State* L, Vector3 v) {
    auto* ud = (Vector3*)lua_newuserdata(L, sizeof(Vector3));
    *ud = v;
    luaL_getmetatable(L, RCBN_VEC3_METATABLE);
    lua_setmetatable(L, -2);
}

void LuauEngine::pushColor4(lua_State* L, Color4 c) {
    auto* ud = (Color4*)lua_newuserdata(L, sizeof(Color4));
    *ud = c;
    luaL_getmetatable(L, RCBN_COLOR4_METATABLE);
    lua_setmetatable(L, -2);
}

void LuauEngine::onGuiButtonActivated(GuiButton* btn) {
    if (!btn || !btn->Activated) return;
    btn->Activated->fire(L, nullptr);
}

// ===================================================
//  Signal メタテーブル
// ===================================================
int LuauEngine::signal_index(lua_State* L) {
    const char* key = luaL_checkstring(L, 2);
    if (strcmp(key, "Connect") == 0) {
        auto* ud = (std::shared_ptr<RCBNScriptSignal>*)luaL_checkudata(L, 1, RCBN_SIGNAL_METATABLE);
        lua_pushlightuserdata(L, ud->get());
        lua_pushcclosure(L, signal_connect_closure, "Connect", 1);
        return 1;
    }
    if (strcmp(key, "Once") == 0) {
        auto* ud = (std::shared_ptr<RCBNScriptSignal>*)luaL_checkudata(L, 1, RCBN_SIGNAL_METATABLE);
        lua_pushlightuserdata(L, ud->get());
        lua_pushcclosure(L, signal_once_closure, "Once", 1);
        return 1;
    }
    if (strcmp(key, "Until") == 0) {
        auto* ud = (std::shared_ptr<RCBNScriptSignal>*)luaL_checkudata(L, 1, RCBN_SIGNAL_METATABLE);
        lua_pushlightuserdata(L, ud->get());
        lua_pushcclosure(L, signal_until_closure, "Until", 1);
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

int LuauEngine::signal_connect_closure(lua_State* L) {
    auto* sig = static_cast<RCBNScriptSignal*>(lua_touserdata(L, lua_upvalueindex(1)));
    // arg1 = self (signal), arg2 = callback function
    int ref = lua_ref(L, 2);
    auto shared = sig->shared_from_this();
    int id = sig->connect(L, ref, false);
    pushConnection(L, std::make_shared<RCBNScriptConnection>(shared, id));
    return 1;
}

int LuauEngine::signal_once_closure(lua_State* L) {
    auto* sig = static_cast<RCBNScriptSignal*>(lua_touserdata(L, lua_upvalueindex(1)));
    // arg1 = self (signal), arg2 = callback function
    int ref = lua_ref(L, 2);
    auto shared = sig->shared_from_this();
    int id = sig->connect(L, ref, true);
    pushConnection(L, std::make_shared<RCBNScriptConnection>(shared, id));
    return 1;
}

int LuauEngine::signal_until_closure(lua_State* L) {
    auto* sig = static_cast<RCBNScriptSignal*>(lua_touserdata(L, lua_upvalueindex(1)));
    // arg1 = self (signal), arg2 = Event userdata, arg3 = callback function
    auto* evtUd = (std::weak_ptr<Instance>*)luaL_checkudata(L, 2, RCBN_INST_METATABLE);
    auto evtInst = evtUd->lock();
    int ref = lua_ref(L, 3);
    auto shared = sig->shared_from_this();
    int id = sig->connect(L, ref, false);
    auto conn = std::make_shared<RCBNScriptConnection>(shared, id);
    if (evtInst && evtInst->IsA("Event")) {
        static_cast<Event*>(evtInst.get())->addUntilConnection(conn);
    }
    pushConnection(L, conn);
    return 1;
}

// ===================================================
//  Connection メタテーブル
// ===================================================
int LuauEngine::connection_index(lua_State* L) {
    const char* key = luaL_checkstring(L, 2);
    if (strcmp(key, "Disconnect") == 0) {
        auto* ud = (std::shared_ptr<RCBNScriptConnection>*)luaL_checkudata(L, 1, RCBN_CONNECTION_METATABLE);
        lua_pushlightuserdata(L, ud->get());
        lua_pushcclosure(L, connection_disconnect_closure, "Disconnect", 1);
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

int LuauEngine::connection_disconnect_closure(lua_State* L) {
    auto* conn = static_cast<RCBNScriptConnection*>(lua_touserdata(L, lua_upvalueindex(1)));
    conn->disconnect();
    return 0;
}

// ===================================================
//  Instance.new
// ===================================================
int LuauEngine::instance_new_closure(lua_State* L) {
    const char* className = luaL_checkstring(L, 1);

    std::shared_ptr<Instance> inst;
    if      (strcmp(className, "Event")        == 0) inst = std::make_shared<Event>();
    else if (strcmp(className, "TextLabel")    == 0) inst = std::make_shared<TextLabel>();
    else if (strcmp(className, "TextButton")   == 0) inst = std::make_shared<TextButton>();
    else if (strcmp(className, "SurfaceGui")   == 0) inst = std::make_shared<SurfaceGui>();
    else if (strcmp(className, "BillboardGui") == 0) inst = std::make_shared<BillboardGui>();
    else if (strcmp(className, "ProximityPrompt") == 0) inst = std::make_shared<ProximityPrompt>();

    if (!inst) { lua_pushnil(L); return 1; }

    s_ownedInstances.push_back(inst);
    auto* ud = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
    new (ud) std::weak_ptr<Instance>(inst);
    luaL_getmetatable(L, RCBN_INST_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

// ===================================================
//  Event:Fire クロージャー
// ===================================================
int LuauEngine::event_fire_closure(lua_State* L) {
    // upvalue 1 = Event の weak_ptr userdata（instance_index が設定）
    // どちらの metatable でも動くよう両方試みる
    Instance* raw = nullptr;
    void* ud = lua_touserdata(L, lua_upvalueindex(1));
    if (ud) {
        // weak_ptr として試みる
        auto* wp = static_cast<std::weak_ptr<Instance>*>(ud);
        if (auto p = wp->lock()) raw = p.get();
    }
    if (!raw) return 0;
    if (raw->IsA("Event")) {
        static_cast<Event*>(raw)->fire();
    }
    return 0;
}

// ===================================================
//  Heartbeat / Collision
// ===================================================
void LuauEngine::setSystem(System* s) {
    m_system = s;
}

void LuauEngine::fireHeartbeat(float dt) {
    if (!m_system || !m_system->Heartbeat) return;
    m_system->Heartbeat->fire(L, [dt](lua_State* Lx) -> int {
        lua_pushnumber(Lx, static_cast<double>(dt));
        return 1;
    });
}

void LuauEngine::onCollision(BaseCube* a, BaseCube* b) {
    if (a && a->Touched) a->Touched->fire(L);
    if (b && b->Touched) b->Touched->fire(L);
}

void LuauEngine::update(float deltaTime) {
    if (m_system) {
        for (auto& [name, child] : m_system->getChildren()) {
            if (!child || !child->IsA("Workspace")) continue;
            auto* ws = static_cast<Workspace*>(child.get());
            auto scripts = ws->scripts;
            for (auto& inst : scripts) {
                auto script = std::dynamic_pointer_cast<Script>(inst);
                if (script && script->Sleeping && script->Coroutine != nullptr) {
                    script->SleepRemaining -= deltaTime;
                    if (script->SleepRemaining <= 0.0f) {
                        script->Sleeping = false;
                        execute(*script);
                    }
                }
            }
        }
        return;
    }

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
