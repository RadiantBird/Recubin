#include "include/Core/LuauEngine.hpp"
#include "include/Core/PropertyRegistry.hpp"
#include "include/Core/LuarCompiler.hpp"
#include "include/Core/FileLoader.hpp"
#include "include/Core/Physics.hpp"
#include "include/Core/RCBNScriptSignal.hpp"
#include "include/Instances/Workspace.hpp"
#include "include/Instances/Sound.hpp"
#include "include/Instances/Humanoid.hpp"
#include "include/Instances/PathfindingService.hpp"
#include "include/Instances/ChatService.hpp"
#include "include/Instances/UserInput.hpp"
#include "include/Core/User.hpp"
#include "include/Instances/Tool.hpp"
#include "include/Instances/Animation.hpp"
#include "include/Instances/System.hpp"
#include "include/Network/NetworkManager.hpp"
#include "include/Instances/Event.hpp"
#include "include/Instances/SignalEvent.hpp"
#include "include/Instances/TextLabel.hpp"
#include "include/Instances/TextButton.hpp"
#include "include/Instances/SurfaceGui.hpp"
#include "include/Instances/BillboardGui.hpp"
#include "include/Instances/ProximityPrompt.hpp"
#include "include/Core/TerrainStreamer.hpp"
#include "include/Util/Color4.hpp"
#include "include/Util/Logger.hpp"
#include "include/Instances/LocalScript.hpp"
#include "include/Instances/ModuleScript.hpp"
#include "include/Instances/Cube.hpp"
#include "include/Instances/Cylinder.hpp"
#include "include/Instances/TriangularPrism.hpp"
#include "include/Instances/Truss.hpp"
#include "include/Instances/Seat.hpp"
#include "include/Instances/Sphere.hpp"
#include "include/Instances/MeshCube.hpp"
#include "include/Instances/LiquidCube.hpp"
#include "include/Instances/Skybox.hpp"
#include "include/Instances/Sun.hpp"
#include "include/Instances/Moon.hpp"
#include "include/Instances/Model.hpp"
#include "include/Instances/Decal.hpp"
#include "include/Instances/Texture.hpp"
#include "include/Instances/Lighting.hpp"
#include "include/Instances/PointLight.hpp"
#include "include/Instances/SpotLight.hpp"
#include "include/Instances/PostEffect.hpp"
#include "include/Instances/Canvas.hpp"
#include "include/imgui/imgui.h"
#include "include/Instances/AppImage.hpp"
#include "include/Instances/FileRef.hpp"
#include "include/Instances/FontFile.hpp"
#include "include/Instances/StarterCharacter.hpp"
#include "include/Core/Terrain.hpp"
#include "include/Core/SystemState.hpp"
#include "include/Instances/Rope.hpp"
#include "include/Instances/Rod.hpp"
#include "include/Instances/Weld.hpp"
#include "include/Instances/Motor.hpp"
#include "include/Instances/BallSocket.hpp"
#include "include/Instances/NoCollision.hpp"
#include "include/Instances/ValueBase.hpp"
#include "include/Instances/IntValue.hpp"
#include "include/Instances/BoolValue.hpp"
#include "include/Instances/NumberValue.hpp"
#include "include/Instances/Vector3Value.hpp"
#include "include/Instances/Color4Value.hpp"
#include "include/Instances/CFrameValue.hpp"
#include "include/Instances/QuaternionValue.hpp"
#include "include/Instances/ObjectValue.hpp"
#include "include/Instances/Attachment.hpp"
#include "include/Instances/Force.hpp"
#include "include/Instances/ImageLabel.hpp"
#include "include/Instances/ImageButton.hpp"
#include "include/Instances/Folder.hpp"
#include "include/Instances/ParticleEmitter.hpp"
#include "include/Instances/Weather.hpp"
#include "include/Core/AudioService.hpp"
#include "include/Core/BaseCubeFactory.hpp"
#include "include/Core/PhysicalFileInstanceRegistry.hpp"
#include <cmath>
#include <algorithm>
#include <float.h>
#include <fenv.h>
#include <limits>
#if defined(__i386__) || defined(__x86_64__)
#include <xmmintrin.h>
#define RCBN_HAS_MXCSR 1
#endif

// DispatchTableの定義
std::unordered_map<std::string_view, std::unordered_map<std::string_view, LuauEngine::GetterFunc>> LuauEngine::DispatchTable;
std::unordered_map<std::string_view, std::unordered_map<std::string_view, LuauEngine::SetterFunc>> LuauEngine::SetterTable;
Script* LuauEngine::currentScript = nullptr;
LuauEngine::EngineTask* LuauEngine::currentTask = nullptr;

namespace {
constexpr const char* RCBN_ORIGINAL_TOSTRING_KEY = "RCBN_OriginalTostring";

bool isTableAt(lua_State* L, int index) {
    return std::string_view(lua_typename(L, lua_type(L, index))) == "table";
}

std::string callOriginalTostring(lua_State* L, int valueIndex, int originalTostringIndex) {
    valueIndex = lua_absindex(L, valueIndex);
    originalTostringIndex = lua_absindex(L, originalTostringIndex);
    const int stackTop = lua_gettop(L);
    lua_pushvalue(L, originalTostringIndex);
    lua_pushvalue(L, valueIndex);
    lua_call(L, 1, 1);

    size_t length = 0;
    const char* text = lua_tolstring(L, -1, &length);
    std::string result = text ? std::string(text, length) : "";
    lua_settop(L, stackTop);
    return result;
}

std::string formatTableValue(lua_State* L, int valueIndex, int originalTostringIndex,
                             std::vector<const void*>& ancestors);

std::string formatTable(lua_State* L, int tableIndex, int originalTostringIndex,
                        std::vector<const void*>& ancestors) {
    tableIndex = lua_absindex(L, tableIndex);
    const void* tablePointer = lua_topointer(L, tableIndex);
    if (std::find(ancestors.begin(), ancestors.end(), tablePointer) != ancestors.end())
        return "<cycle>";

    ancestors.push_back(tablePointer);
    std::string result = "{";
    bool first = true;
    lua_pushnil(L);
    while (lua_next(L, tableIndex) != 0) {
        if (!first) result += ", ";
        first = false;
        result += "[" + formatTableValue(L, -2, originalTostringIndex, ancestors) + "] = ";
        result += formatTableValue(L, -1, originalTostringIndex, ancestors);
        lua_pop(L, 1); // value; keep key for lua_next
    }
    result += "}";
    ancestors.pop_back();
    return result;
}

std::string formatTableValue(lua_State* L, int valueIndex, int originalTostringIndex,
                             std::vector<const void*>& ancestors) {
    if (!isTableAt(L, valueIndex))
        return callOriginalTostring(L, valueIndex, originalTostringIndex);
    return formatTable(L, valueIndex, originalTostringIndex, ancestors);
}
} // namespace

// Instance.new で生成したインスタンスの所有権を保持するストレージ。
// Lua に渡す userdata は weak_ptr なので、親付け前(=ツリー未接続)の期間だけここで強参照を保持し、
// 即時 GC を防ぐ。親付け後はツリー(親の children)が所有するため、sweepOwnedInstances が手放す。
static std::vector<std::shared_ptr<Instance>> s_ownedInstances;

void LuauEngine::sweepOwnedInstances() {
    auto& v = s_ownedInstances;
    v.erase(std::remove_if(v.begin(), v.end(), [](const std::shared_ptr<Instance>& sp) {
        // 破棄済み or 親付け済み(ツリーが所有)なら強参照を手放す。未親付けは保持し続ける。
        return !sp || !sp->Parent.expired();
    }), v.end());
}

std::shared_ptr<Workspace> LuauEngine::resolveScriptWorkspace(Script& script) {
    Instance* ws = script.findFirstAncestorWorkspace();
    if (ws) return std::static_pointer_cast<Workspace>(ws->shared_from_this());
    // Workspace外(System配下)のスクリプトはアクティブWorkspaceを参照する
    return workspace.lock();
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
#ifdef RCBN_HAS_MXCSR
    unsigned int mxcsr;
#endif
};

FPUState saveFPU() {
    FPUState s;
    fegetenv(&s.env);
#ifdef RCBN_HAS_MXCSR
    s.mxcsr = _mm_getcsr();
#endif
    return s;
}

void restoreFPU(const FPUState& s) {
    fesetenv(&s.env);
#ifdef RCBN_HAS_MXCSR
    _mm_setcsr(s.mxcsr);
#endif
}

// Clone/Restart呼び出し元のScriptを識別するラベル。Heartbeat経由の呼び出しは
// currentScriptが設定されないため専用ラベルにまとめる。
static std::string scriptSafetyLabel(Script* s) {
    if (!s) return "Unknown/Heartbeat";
    return s->Name.empty() ? "(unnamed Script)" : s->Name;
}

static std::string scriptExecutionLabel(Script* script) {
    if (!script) return "Unknown signal listener";
    std::string label = script->Name.empty() ? "(unnamed Script)" : script->Name;
    if (!script->Path.empty()) label += " (" + script->Path + ")";
    return label;
}

void LuauEngine::beginProtectedExecution() {
    m_lastTraceback.clear();
}

std::string LuauEngine::consumeProtectedError(lua_State* errorState) {
    std::string traceback = std::move(m_lastTraceback);
    m_lastTraceback.clear();
    if (traceback.empty() && errorState) {
        const char* directTrace = lua_debugtrace(errorState);
        if (directTrace) traceback = directTrace;
    }

    std::string message = "unknown error";
    if (errorState && lua_gettop(errorState) > 0) {
        const char* raw = lua_tostring(errorState, -1);
        if (raw) {
            message = raw;
            lua_pop(errorState, 1);
        } else {
            const char* typeName = lua_typename(errorState, lua_type(errorState, -1));
            message = std::string("non-string error object") + (typeName ? " (" + std::string(typeName) + ")" : "");
            lua_pop(errorState, 1);
        }
    }

    if (!traceback.empty() && traceback.find(message) == std::string::npos)
        return message + "\n" + traceback;
    return traceback.empty() ? message : traceback;
}

void LuauEngine::reportProtectedError(lua_State* errorState, const std::string& context) {
    reportProtectedMessage(context, consumeProtectedError(errorState));
}

void LuauEngine::reportProtectedMessage(const std::string& context, const std::string& message) {
    const std::string output = "[" + context + "] " + message;
    std::cerr << output << "\n";
    if (g_luauLogHook) g_luauLogHook("[ERROR] " + output);
}

std::chrono::steady_clock::time_point LuauEngine::beginSignalCallback() {
    const auto previousStart = m_scriptResumeStart;
    ++m_signalCallbackDepth;
    m_scriptResumeStart = std::chrono::steady_clock::now();
    return previousStart;
}

void LuauEngine::endSignalCallback(std::chrono::steady_clock::time_point previousStart) {
    const auto callbackElapsed = std::chrono::steady_clock::now() - m_scriptResumeStart;
    if (m_signalCallbackDepth > 0) --m_signalCallbackDepth;
    // Do not charge time spent in an isolated listener to an enclosing Script
    // or Task; otherwise a timed-out listener immediately times out its caller.
    m_scriptResumeStart = previousStart + callbackElapsed;
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

    // H-2: registerClass 済みだが applyToDispatch されず Luau から不可視のクラスを検出して警告。
    // 自動公開はしない（配線忘れをサイレントにせず、開発時に気付けるようにするのが目的）。
    for (std::string_view cls : PropertyRegistry::registeredClassNames()) {
        // 独自プロパティを持つクラスのみ対象（空スキーマ=継承のみの Moon/PointLight 等は
        // DispatchTable に独自エントリが無くて正常なので誤検出しない）。
        if (!PropertyRegistry::schemaFor(cls).empty() && DispatchTable.find(cls) == DispatchTable.end())
            RCBN_WARN("PropertyRegistry class '" << std::string(cls)
                << "' は registerClass 済みですが applyToDispatch されていません（Luau から不可視）。"
                   "InitDispatchTable_* に applyToDispatch を追加してください。");
    }
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

    // Quaternion metatable
    luaL_newmetatable(L, RCBN_QUATERNION_METATABLE);
    lua_pushcfunction(L, quat_index,    "quat_index");    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, quat_newindex, "quat_newindex"); lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, quat_tostring, "quat_tostring"); lua_setfield(L, -2, "__tostring");
    lua_pushcfunction(L, quat_mul, "quat_mul"); lua_setfield(L, -2, "__mul");
    lua_pushcfunction(L, quat_eq,  "quat_eq");  lua_setfield(L, -2, "__eq");
    lua_pop(L, 1);

    // CFrame metatable
    luaL_newmetatable(L, RCBN_CFRAME_METATABLE);
    lua_pushcfunction(L, cframe_index,    "cframe_index");    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, cframe_newindex, "cframe_newindex"); lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, cframe_tostring, "cframe_tostring"); lua_setfield(L, -2, "__tostring");
    lua_pushcfunction(L, cframe_mul, "cframe_mul"); lua_setfield(L, -2, "__mul");
    lua_pushcfunction(L, cframe_eq,  "cframe_eq");  lua_setfield(L, -2, "__eq");
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

    // Register Color4 with new/fromRGBA methods
    lua_newtable(L);
    lua_pushcfunction(L, color4_constructor, "new");
    lua_setfield(L, -2, "new");
    lua_pushcfunction(L, color4_from_rgba, "fromRGBA");
    lua_setfield(L, -2, "fromRGBA");
    lua_setglobal(L, "Color4");

    // Register Vector2 with new method
    lua_newtable(L);
    lua_pushcfunction(L, vec2_constructor, "new");
    lua_setfield(L, -2, "new");
    lua_setglobal(L, "Vector2");

    // Register Quaternion with new / fromEuler / fromAxisAngle / Slerp
    lua_newtable(L);
    lua_pushcfunction(L, quat_constructor,    "new");          lua_setfield(L, -2, "new");
    lua_pushcfunction(L, quat_from_euler,     "fromEuler");    lua_setfield(L, -2, "fromEuler");
    lua_pushcfunction(L, quat_from_axis_angle,"fromAxisAngle");lua_setfield(L, -2, "fromAxisAngle");
    lua_pushcfunction(L, quat_slerp,          "Slerp");        lua_setfield(L, -2, "Slerp");
    lua_pushcfunction(L, quat_look_rotation,  "LookRotation"); lua_setfield(L, -2, "LookRotation");
    lua_setglobal(L, "Quaternion");

    // Register CFrame with new / fromAxisAngle
    lua_newtable(L);
    lua_pushcfunction(L, cframe_constructor,     "new");          lua_setfield(L, -2, "new");
    lua_pushcfunction(L, cframe_from_axis_angle, "fromAxisAngle");lua_setfield(L, -2, "fromAxisAngle");
    lua_pushcfunction(L, cframe_look_at,         "lookAt");       lua_setfield(L, -2, "lookAt");
    lua_setglobal(L, "CFrame");

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

    lua_pushcfunction(L, luafn_assert, "assert");
    lua_setglobal(L, "assert");

    // Retain the standard implementation once per Lua state. Scripts receive
    // their own globals, so looking up tostring after an override could capture
    // this formatter instead of the original function.
    lua_getfield(L, LUA_REGISTRYINDEX, RCBN_ORIGINAL_TOSTRING_KEY);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_getglobal(L, "tostring");
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, RCBN_ORIGINAL_TOSTRING_KEY);
    }

    lua_pushvalue(L, -1);
    lua_pushcclosure(L, global_tostring, "tostring", 1);
    lua_setglobal(L, "tostring");

    lua_pushvalue(L, -1);
    lua_pushcclosure(L, global_print_message, "print", 1);
    lua_setglobal(L, "print");
    lua_pop(L, 1); // original tostring
    
    lua_pushcfunction(L, wait, "wait");
    lua_setglobal(L, "wait");

    lua_pushcfunction(L, luafn_require, "require");
    lua_setglobal(L, "require");

    // task モジュール(task.wait は wait のエイリアス)
    lua_newtable(L);
    lua_pushcfunction(L, task_spawn, "spawn"); lua_setfield(L, -2, "spawn");
    lua_pushcfunction(L, task_delay, "delay"); lua_setfield(L, -2, "delay");
    lua_pushcfunction(L, wait,       "wait");  lua_setfield(L, -2, "wait");
    lua_setglobal(L, "task");

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

    // M-1: 最派生クラス名をキーに、継承チェーン上の全プロパティをマージした表をキャッシュ。
    // 初回のみ IsA ループを実行し、以降は O(1) ルックアップ（毎回の全クラス走査を排除）。
    // 不変条件: 基底と派生で同名プロパティを定義しないこと（衝突時の優先順位は未規定）。
    // 値は関数を「コピー」で保持する（DispatchTable 再構築で参照が無効化されないように）。
    static std::unordered_map<std::string,
        std::unordered_map<std::string_view, GetterFunc>> s_cache;
    const std::string& cls = obj->getClassName();
    auto cit = s_cache.find(cls);
    if (cit == s_cache.end()) {
        std::unordered_map<std::string_view, GetterFunc> merged;
        for (const auto& [className, classProps] : DispatchTable)
            if (obj->IsA(std::string(className)))
                for (const auto& [pname, fn] : classProps)
                    merged.emplace(pname, fn);
        cit = s_cache.emplace(cls, std::move(merged)).first;
    }
    if (auto it = cit->second.find(key); it != cit->second.end())
        return it->second(L, obj);

    // プロパティが無ければ同名の子インスタンスを返す（Roblox のドットチェーン相当）
    // 例: workspace.PlayerCharacter.Humanoid
    const auto& kids = obj->getChildren();
    if (auto it = kids.find(std::string(key)); it != kids.end() && it->second) {
        auto* ud = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
        new (ud) std::weak_ptr<Instance>(it->second);
        luaL_getmetatable(L, RCBN_INST_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
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

    // ループタイムアウト検出。lua_Callbacksは全コルーチンで共有されるためLに一度だけ設定すれば
    // 全てのScriptコルーチンに適用される。currentScript/currentTaskが共にnullの間
    // (Heartbeat経由の直接pcall等)は対象が特定できないためチェックをスキップする。
    lua_callbacks(L)->interrupt = [](lua_State* Lco, int gc) {
        auto* engine = static_cast<LuauEngine*>(lua_callbacks(Lco)->userdata);
        if (gc >= 0 || (!currentScript && !currentTask && (!engine || engine->m_signalCallbackDepth == 0))) return;
        if (!engine || !engine->m_system) return;
        float limit = engine->m_system->ScriptLoopTimeoutSeconds;
        if (limit <= 0.0f) return; // 0以下でチェック無効
        float elapsed = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - engine->m_scriptResumeStart).count();
        if (elapsed >= limit) {
            luaL_error(Lco, "Script loop timeout exceeded (%.2fs) - possible infinite loop", limit);
        }
    };

    InitMetatables();
    InitDispatchTable();
    InitSetterTable();
}

LuauEngine::~LuauEngine() {
    if (L) {
        cancelAllTasks();
        lua_close(L);
    }
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

void LuauEngine::clearGlobalInstance(const std::string& name) {
    lua_pushnil(L);
    lua_setglobal(L, name.c_str());
}

int LuauEngine::instance_newindex(lua_State* L) {
    auto* userdata = (std::weak_ptr<Instance>*)luaL_checkudata(L, 1, RCBN_INST_METATABLE);
    auto obj_shared = userdata->lock();
    if (!obj_shared) return 0;
    Instance* obj = obj_shared.get();
    std::string_view key = luaL_checkstring(L, 2);

    // M-1: setter も同様にクラス名キーでキャッシュ（instance_index と対）。
    // 値は関数をコピーで保持（SetterTable 再構築で無効化されないように）。
    static std::unordered_map<std::string,
        std::unordered_map<std::string_view, SetterFunc>> s_cache;
    const std::string& cls = obj->getClassName();
    auto cit = s_cache.find(cls);
    if (cit == s_cache.end()) {
        std::unordered_map<std::string_view, SetterFunc> merged;
        for (const auto& [className, classProps] : SetterTable)
            if (obj->IsA(std::string(className)))
                for (const auto& [pname, fn] : classProps)
                    merged.emplace(pname, fn);
        cit = s_cache.emplace(cls, std::move(merged)).first;
    }
    if (auto it = cit->second.find(key); it != cit->second.end())
        return it->second(L, obj);

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

bool LuauEngine::loadScriptChunk(lua_State* co, Script& script) {
    // ファイルが真実の源: 実行直前に最新ソースを再読込する
    // (外部エディタ編集・新規作成直後の内容を反映。空読込時は既存を保持)
    if (!script.isPrecompiled && !script.Path.empty()) {
        std::string latest = FileLoader::readText(script.Path);
        if (!latest.empty()) script.Source = latest;
    }

    std::string compiledSource;
    const std::string* sourcePtr = &script.Source;

    // .luarファイルはRust製LuarコンパイラでLuauに変換する
    auto endsWithLuar = [](const std::string& s) {
        return s.size() >= 5 && s.substr(s.size() - 5) == ".luar";
    };
    bool isLuar = endsWithLuar(script.Name) || endsWithLuar(script.Path);
    if (isLuar) {
        static LuarCompiler s_luarCompiler;
        compiledSource = s_luarCompiler.compile(script.Source, script.Path);
        if (compiledSource.empty()) { script.Aborted = true; return false; }
        RCBN_LOG("\033[32m Compiling Luar Source has succeeded!\033[0m");
        // std::cerr << "[LuarCompiler] Output:\n" << compiledSource << "\n---\n";
        sourcePtr = &compiledSource;
    }

    const std::string& source = *sourcePtr;
    int status;
    beginProtectedExecution();
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
        reportProtectedError(co, "Script Load | " + scriptExecutionLabel(&script));
        return false;
    }
    return true;
}

bool LuauEngine::execute(Script& script) {
    // 自分自身へのRestart()はここでループして即座に再実行する(再帰しない)。
    // これによりC++呼び出しスタックを伸ばさずに「同一フレームで即再実行」を実現する。
    for (;;) {
        // 既にコルーチンがある場合は再開、なければ新規作成
        if (script.Coroutine == nullptr) {
            script.Coroutine = lua_newthread(L);
            // レジストリ参照でGCから保護する。Lのスタックには積みっぱなしにしない
            // (積みっぱなしだと再起動のたびにLのスタックが無制限に伸びてGCが壊れる)
            script.CoroutineRef = lua_ref(L, -1);
            lua_pop(L, 1);

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
        setWorkspaceGlobal(co, resolveScriptWorkspace(script));

        // 初回実行の場合、スクリプトをロード
        // lua_status(): 0=OK, LUA_YIELD=suspended, LUA_ERRERR=error, etc.
        if (lua_status(co) == 0 && lua_gettop(co) == 0) {  // スタックが空なら初回実行
            if (!loadScriptChunk(co, script)) return false;
        }

        // currentScript を設定
        currentScript = &script;

        // コルーチンを再開
        int nargs = 0;
        FPUState fpuState = saveFPU();
        m_scriptResumeStart = std::chrono::steady_clock::now();
        beginProtectedExecution();
        int result = lua_resume(co, L, nargs);
        restoreFPU(fpuState);

        // 結果を確認
        if (result == LUA_YIELD) {
            // wait() で停止した - Script の Sleeping フラグは wait() 内で設定済み
            currentScript = nullptr;
            return true;
        } else if (script.Coroutine != co) {
            // 実行中に自分自身へRestart()が呼ばれ、Coroutineが差し替え済み。
            // このcoの完了/エラー結果でrestart()後の新しい状態を上書きせず、
            // ループして新しいコルーチンを同一フレーム内で即座に実行する。
            currentScript = nullptr;
            continue;
        } else if (result == 0) {
            // 完了
            script.Sleeping = false;
            script.Completed = true;  // 完了フラグをセット
            lua_unref(L, script.CoroutineRef);
            script.CoroutineRef = -1;
            script.Coroutine = nullptr;  // コルーチンをクリア
            currentScript = nullptr;
            return true;
        } else {
            // エラー
            script.Aborted = true; // DO NOT loop on errored script!
            // Luauはエラーメッセージをcoではなく親スレッドLに積む場合がある
            lua_State* errState = (lua_gettop(co) > 0) ? co : L;
            reportProtectedError(errState, "Script | " + scriptExecutionLabel(&script));
            lua_unref(L, script.CoroutineRef);
            script.CoroutineRef = -1;
            script.Coroutine = nullptr;
            currentScript = nullptr;
            return false;
        }
    }
}

// Luau には luaL_testudata が無いため、メタテーブル比較で型を判定する（一致しなければ nullptr、エラーは投げない）
static std::weak_ptr<Instance>* testInstanceUserdata(lua_State* L, int idx) {
    void* p = lua_touserdata(L, idx);
    if (!p) return nullptr;
    if (!lua_getmetatable(L, idx)) return nullptr;
    luaL_getmetatable(L, LuauEngine::RCBN_INST_METATABLE);
    bool same = lua_rawequal(L, -1, -2) != 0;
    lua_pop(L, 2);
    return same ? (std::weak_ptr<Instance>*)p : nullptr;
}

// ==================== Workspace Methods ====================
int LuauEngine::workspace_raycast_closure(lua_State* L) {
    auto* ptr = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto ws_shared = ptr->lock();
    if (!ws_shared) return 0;
    Workspace* ws = static_cast<Workspace*>(ws_shared.get());

    // L[1] = self, L[2] = origin, L[3] = direction,
    // L[4] = maxDistance または legacy の除外Instance, L[5] = 除外Instance
    Vector3* origin    = (Vector3*)luaL_checkudata(L, 2, RCBN_VEC3_METATABLE);
    Vector3* direction = (Vector3*)luaL_checkudata(L, 3, RCBN_VEC3_METATABLE);

    float maxDistance = (std::numeric_limits<float>::max)();
    int ignoreInstanceIndex = 4;
    if (lua_isnumber(L, 4)) {
        double suppliedMaxDistance = lua_tonumber(L, 4);
        if (!std::isfinite(suppliedMaxDistance) || suppliedMaxDistance <= 0.0 ||
            suppliedMaxDistance > static_cast<double>((std::numeric_limits<float>::max)())) {
            luaL_argerror(L, 4, "maxDistance must be a finite positive number within the supported range");
            return 0;
        }
        maxDistance = static_cast<float>(suppliedMaxDistance);
        ignoreInstanceIndex = 5;
    } else if (lua_isnoneornil(L, 4) && !lua_isnoneornil(L, 5)) {
        // maxDistance を明示的に nil にした場合は、第5引数の除外Instanceを受け付ける。
        ignoreInstanceIndex = 5;
    }

    Physics* physics = ws->getPhysicsEngine();
    if (!physics) {
        lua_pushnil(L);
        return 1;
    }

    // 除外Instance（省略可）。BaseCube系ならその物理ボディをraycastの除外対象にする
    const BaseCube* ignoreCube = nullptr;
    if (!lua_isnoneornil(L, ignoreInstanceIndex)) {
        auto* iud = testInstanceUserdata(L, ignoreInstanceIndex);
        if (iud) {
            auto ignoreInst = iud->lock();
            if (ignoreInst && ignoreInst->IsA("BaseCube"))
                ignoreCube = static_cast<BaseCube*>(ignoreInst.get());
        }
    }

    RaycastHit hit;
    bool didHit = physics->raycast(*origin, *direction, maxDistance, hit, ignoreCube);

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

// ==================== Canvas methods ====================
int LuauEngine::canvas_set_pixel_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto self = ud->lock();
    if (!self || !self->IsA("Canvas")) { lua_pushboolean(L, 0); return 1; }

    // L[1]=self, L[2]=x, L[3]=y, L[4]=color
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);
    Color4* col = (Color4*)luaL_checkudata(L, 4, RCBN_COLOR4_METATABLE);

    bool ok = static_cast<Canvas*>(self.get())->setPixel(x, y, *col);
    lua_pushboolean(L, ok);
    return 1;
}

int LuauEngine::canvas_get_pixel_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto self = ud->lock();
    if (!self || !self->IsA("Canvas")) { lua_pushnil(L); return 1; }

    // L[1]=self, L[2]=x, L[3]=y
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);

    Color4 out;
    if (!static_cast<Canvas*>(self.get())->getPixel(x, y, out)) { lua_pushnil(L); return 1; }

    Color4* col = (Color4*)lua_newuserdata(L, sizeof(Color4));
    *col = out;
    luaL_getmetatable(L, RCBN_COLOR4_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

int LuauEngine::canvas_clear_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto self = ud->lock();
    if (!self || !self->IsA("Canvas")) return 0;
    Canvas* canvas = static_cast<Canvas*>(self.get());

    // L[1]=self, L[2]=color（省略可。省略時はBackgroundColor）
    Color4 c = canvas->BackgroundColor;
    if (!lua_isnoneornil(L, 2)) {
        Color4* col = (Color4*)luaL_checkudata(L, 2, RCBN_COLOR4_METATABLE);
        c = *col;
    }
    canvas->clear(c);
    return 0;
}

int LuauEngine::canvas_world_to_uv_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto self = ud->lock();
    if (!self || !self->IsA("Canvas")) { lua_pushnil(L); return 1; }

    // L[1]=self, L[2]=position(Vector3)
    Vector3* pos = (Vector3*)luaL_checkudata(L, 2, RCBN_VEC3_METATABLE);

    Vector2 uv;
    if (!static_cast<Canvas*>(self.get())->worldToUV(*pos, uv)) { lua_pushnil(L); return 1; }

    pushVector2(L, uv);
    return 1;
}

int LuauEngine::pathfinding_find_path_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto self = ud->lock();
    auto* engine = static_cast<LuauEngine*>(lua_callbacks(L)->userdata);
    if (!self || !engine) {
        lua_newtable(L);
        return 1;
    }

    // L[1]=self, L[2]=workspace, L[3]=start, L[4]=goal
    auto* wsUd = (std::weak_ptr<Instance>*)luaL_checkudata(L, 2, RCBN_INST_METATABLE);
    auto wsInst = wsUd->lock();
    Workspace* workspace = wsInst ? dynamic_cast<Workspace*>(wsInst.get()) : nullptr;
    if (!workspace) {
        lua_newtable(L);
        return 1;
    }

    Vector3* start = (Vector3*)luaL_checkudata(L, 3, RCBN_VEC3_METATABLE);
    Vector3* goal  = (Vector3*)luaL_checkudata(L, 4, RCBN_VEC3_METATABLE);

    auto service = std::dynamic_pointer_cast<PathfindingService>(self);
    auto request = service->RequestFindPath(workspace, *start, *goal);
    if (request.status == PathfindingService::RequestStatus::Ready) {
        engine->pushPathResult(L, request.waypoints);
        return 1;
    }
    if (request.status != PathfindingService::RequestStatus::Pending) {
        lua_newtable(L);
        return 1;
    }

    // require() のモジュール本体など、Luau VMがyieldを許可しない文脈では
    // バックグラウンド生成だけを継続し、同期ビルドへフォールバックしない。
    if (!lua_isyieldable(L)) {
        service->AbandonRequest(request.id);
        RCBN_WARN("PathfindingService: FindPath cannot wait in a non-yieldable context; returning an empty path");
        lua_newtable(L);
        return 1;
    }

    auto pending = std::make_unique<PendingPathCoroutine>();
    pending->service = service;
    pending->requestId = request.id;
    pending->co = L;
    lua_pushthread(L);
    pending->coRef = lua_ref(L, -1);
    lua_pop(L, 1);

    if (currentTask && currentTask->co == L) {
        pending->owner = PathCoroutineOwner::EngineTask;
        pending->task = currentTask;
        pending->context = "Task | " + currentTask->sourceLabel;
        currentTask->waitingForPath = true;
    } else if (currentScript && currentScript->Coroutine == L) {
        pending->owner = PathCoroutineOwner::Script;
        pending->script = std::static_pointer_cast<Script>(currentScript->shared_from_this());
        pending->context = "Script | " + scriptExecutionLabel(currentScript);
        currentScript->WaitingForPath = true;
    } else if (engine->m_signalCallbackDepth > 0) {
        pending->owner = PathCoroutineOwner::Signal;
        pending->context = "Signal | PathfindingService::FindPath";
    } else {
        lua_unref(L, pending->coRef);
        service->AbandonRequest(request.id);
        RCBN_WARN("PathfindingService: FindPath has no supported coroutine owner; returning an empty path");
        lua_newtable(L);
        return 1;
    }

    engine->m_pendingPaths.push_back(std::move(pending));
    return lua_yield(L, 0);
}

void LuauEngine::pushPathResult(
    lua_State* state,
    const std::vector<Pathfinding::PathWaypoint>& waypoints)
{
    lua_newtable(state);
    int idx = 1;
    for (const auto& wp : waypoints) {
        lua_newtable(state);

        Vector3* p = (Vector3*)lua_newuserdata(state, sizeof(Vector3));
        *p = wp.Position;
        luaL_getmetatable(state, RCBN_VEC3_METATABLE);
        lua_setmetatable(state, -2);
        lua_setfield(state, -2, "Position");

        lua_pushstring(state, wp.Action == Pathfinding::WaypointAction::Jump ? "Jump" : "Walk");
        lua_setfield(state, -2, "Action");
        lua_rawseti(state, -2, idx++);
    }
}

bool LuauEngine::isPathfindingCoroutine(lua_State* co) const {
    return std::any_of(m_pendingPaths.begin(), m_pendingPaths.end(),
        [co](const std::unique_ptr<PendingPathCoroutine>& pending) {
            return pending && pending->co == co;
        });
}

void LuauEngine::resumePathScript(
    PendingPathCoroutine& pending,
    const std::vector<Pathfinding::PathWaypoint>& waypoints)
{
    auto script = pending.script.lock();
    if (!script || script->Coroutine != pending.co) return;

    script->WaitingForPath = false;
    setWorkspaceGlobal(pending.co, resolveScriptWorkspace(*script));
    currentScript = script.get();
    pushPathResult(pending.co, waypoints);

    FPUState fpuState = saveFPU();
    m_scriptResumeStart = std::chrono::steady_clock::now();
    beginProtectedExecution();
    int result = lua_resume(pending.co, L, 1);
    restoreFPU(fpuState);

    if (result == LUA_YIELD) {
        currentScript = nullptr;
        return;
    }
    if (script->Coroutine != pending.co) {
        currentScript = nullptr;
        execute(*script);
        return;
    }
    if (result == 0) {
        script->Sleeping = false;
        script->Completed = true;
    } else {
        script->Aborted = true;
        lua_State* errState = lua_gettop(pending.co) > 0 ? pending.co : L;
        reportProtectedError(errState, pending.context);
    }
    if (script->CoroutineRef != -1) lua_unref(L, script->CoroutineRef);
    script->CoroutineRef = -1;
    script->Coroutine = nullptr;
    currentScript = nullptr;
}

void LuauEngine::resumePathSignal(
    PendingPathCoroutine& pending,
    const std::vector<Pathfinding::PathWaypoint>& waypoints)
{
    if (!pending.co) return;
    pushPathResult(pending.co, waypoints);
    const auto previousStart = beginSignalCallback();
    beginProtectedExecution();
    int result = lua_resume(pending.co, L, 1);
    endSignalCallback(previousStart);
    if (result == LUA_YIELD) {
        // FindPathから連続して別のFindPathを呼んだ場合だけ待機を継続する。
        const bool queuedAnotherPath = std::any_of(
            m_pendingPaths.begin(), m_pendingPaths.end(),
            [&pending](const std::unique_ptr<PendingPathCoroutine>& candidate) {
                return candidate && candidate.get() != &pending &&
                       candidate->co == pending.co;
            });
        if (!queuedAnotherPath)
            reportProtectedMessage(pending.context,
                                   "Signal callback yielded; yielding listeners are not supported");
    } else if (result != 0) {
        reportProtectedError(pending.co, pending.context);
    }
}

void LuauEngine::pollPathfindingRequests() {
    // resume中に別のFindPathが登録されても、追加分は次フレームから処理する。
    const size_t count = m_pendingPaths.size();
    for (size_t i = 0; i < count; ++i) {
        PendingPathCoroutine* queued = m_pendingPaths[i].get();
        if (!queued || !queued->co) continue;
        auto service = queued->service.lock();
        std::vector<Pathfinding::PathWaypoint> waypoints;
        PathfindingService::RequestStatus status = PathfindingService::RequestStatus::Cancelled;
        if (service)
            status = service->PollRequest(queued->requestId, waypoints);
        if (status == PathfindingService::RequestStatus::Pending) continue;

        // resumeは新しいFindPath要求をvectorへ追加しうるため、先に所有権を外へ移す。
        auto pendingPtr = std::move(m_pendingPaths[i]);
        PendingPathCoroutine& pending = *pendingPtr;
        if (status != PathfindingService::RequestStatus::Ready)
            RCBN_WARN("PathfindingService: asynchronous FindPath failed or was cancelled");

        if (pending.owner == PathCoroutineOwner::Script) {
            resumePathScript(pending, waypoints);
        } else if (pending.owner == PathCoroutineOwner::EngineTask) {
            if (pending.task && !pending.task->finished && pending.task->co == pending.co) {
                pending.task->waitingForPath = false;
                pushPathResult(pending.co, waypoints);
                resumeEngineTask(*pending.task, 1);
            }
        } else {
            resumePathSignal(pending, waypoints);
        }
        if (pending.coRef != -1) lua_unref(L, pending.coRef);
    }
    m_pendingPaths.erase(
        std::remove(m_pendingPaths.begin(), m_pendingPaths.end(), nullptr),
        m_pendingPaths.end());
}

int LuauEngine::pathfinding_configure_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto self = ud->lock();
    if (!self) return 0;

    // L[1]=self, L[2]=config table（AgentRadius/AgentHeight/AgentMaxClimb/
    // AgentMaxSlope/MaxJumpDistance/MaxJumpHeightを部分指定可。省略キーは現状維持）
    // 注: このLuauビルドはヘッダのlua_Type列挙値とランタイムの実際の型タグがずれているため
    // （lua_type()の生値とLUA_TTABLE等のヘッダ定数が一致しない）、lua_istable/luaL_checktype
    // 等のマクロ・関数は使わず、lua_typename()の文字列名で型判定する
    if (std::string_view(lua_typename(L, lua_type(L, 2))) != "table") {
        luaL_error(L, "Configure: table argument expected");
        return 0;
    }

    for (const auto& d : PropertyRegistry::schemaFor("PathfindingService")) {
        if (d.kind != PropKind::Field || !d.set) continue;
        std::string key(d.name);
        lua_getfield(L, 2, key.c_str());
        if (lua_isnumber(L, -1)) {
            float v = (float)lua_tonumber(L, -1);
            if (d.clampOnLuaWrite && d.lo < d.hi) v = std::clamp(v, d.lo, d.hi);
            d.set(self.get(), PropValue(v));
        }
        lua_pop(L, 1);
    }
    return 0;
}

int LuauEngine::chat_send_message_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto self = ud ? ud->lock() : nullptr;
    if (!self || !self->IsA("ChatService")) return 0;
    size_t len = 0;
    const char* text = luaL_checklstring(L, 2, &len);
    static_cast<ChatService*>(self.get())->sendMessage(std::string(text, len));
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

int LuauEngine::luafn_assert(lua_State* L) {
    std::string message = "assertion failed";
    if (!lua_isnoneornil(L, 2)) {
        message = luaL_checkstring(L, 2);
    }

    if (!lua_toboolean(L, 1)) {
        std::string s = "[Luau] Assertion failed: " + message;
        std::cout << s << std::endl;
        luaL_error(L, "%s", s.c_str());
    }

    lua_pushvalue(L, 1);
    return 1;
}

int LuauEngine::global_tostring(lua_State* L) {
    if (!isTableAt(L, 1)) {
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_pushvalue(L, 1);
        lua_call(L, 1, 1);
        return 1;
    }

    std::vector<const void*> ancestors;
    const std::string text = formatTable(L, 1, lua_upvalueindex(1), ancestors);
    lua_pushlstring(L, text.c_str(), text.size());
    return 1;
}

int LuauEngine::wait(lua_State* L) {
    // 引数省略時は0秒(次フレーム再開)。task.wait()の書き心地を優先
    float s = (float)luaL_optnumber(L, 1, 0.0);

    // 現在実行中のスクリプトに待機情報を記録
    if (currentScript != nullptr) {
        currentScript->Sleeping = true;
        currentScript->SleepTime = s;
        currentScript->SleepRemaining = s;
    } else if (currentTask != nullptr) {
        // task.spawn/task.delayで起動したエンジンタスク内のwait()
        currentTask->sleeping = true;
        currentTask->sleepRemaining = s;
    }

    // スクリプト実行を一時停止
    return lua_yield(L, 0);
}

// ===================================================
//  task モジュール (spawn / delay / wait)
// ===================================================
// このLuauビルドはlua_Type列挙値とランタイムの型タグがずれているため、
// lua_isfunction等のマクロではなくlua_typename()の文字列で型判定する
// (pathfinding_configure_closureの注記参照)
static bool isLuaFunctionAt(lua_State* L, int idx) {
    return std::string_view(lua_typename(L, lua_type(L, idx))) == "function";
}

LuauEngine::EngineTask* LuauEngine::createEngineTask(lua_State* L, int fnIdx, float delaySec) {
    if (!isLuaFunctionAt(L, fnIdx)) {
        luaL_error(L, "task: function argument expected");
        return nullptr; // 到達しない
    }

    auto* engine = static_cast<LuauEngine*>(lua_callbacks(L)->userdata);
    if (!engine) {
        luaL_error(L, "task: engine unavailable");
        return nullptr; // 到達しない
    }

    // 安全対策: 1フレームあたりのタスク生成上限(無限spawn再帰の抑止)
    if (engine->m_system) {
        std::string label = scriptSafetyLabel(currentScript);
        int total = ++engine->m_totalTasksThisFrame;
        engine->m_taskCallCounts[label]++;
        if (total > engine->m_system->MaxTasksPerFrame) {
            engine->reportSafetyBreach("Infinite task spawning possible", engine->m_taskCallCounts);
            luaL_error(L, "Safety limit exceeded: too many task.spawn/delay calls this frame (max %d)",
                       engine->m_system->MaxTasksPerFrame);
            return nullptr; // 到達しない
        }
    }

    int nargs = lua_gettop(L) - fnIdx; // fnより後ろの可変引数の個数

    auto task = std::make_unique<EngineTask>();
    task->sourceLabel = currentScript ? scriptExecutionLabel(currentScript)
                                      : (currentTask ? currentTask->sourceLabel : "Unknown task source");
    task->co = lua_newthread(L);
    task->coRef = lua_ref(L, -1); // レジストリ参照でGCから保護
    lua_pop(L, 1);
    task->delayRemaining = delaySec;
    task->pendingArgs    = nargs;

    // コルーチンにもデバッグコールバックを伝播させる(execute()と同じパターン)
    lua_callbacks(task->co)->userdata = lua_callbacks(L)->userdata;

    // fn+可変引数(スタック末尾のnargs+1個)をタスクのコルーチンへ移す
    lua_xmove(L, task->co, nargs + 1);

    engine->m_tasks.push_back(std::move(task));
    return engine->m_tasks.back().get();
}

int LuauEngine::task_spawn(lua_State* L) {
    // L[1]=fn, L[2..]=引数。即時に1回resumeする(Roblox task.spawn相当)
    EngineTask* task = createEngineTask(L, 1, 0.0f);
    auto* engine = static_cast<LuauEngine*>(lua_callbacks(L)->userdata);
    engine->resumeEngineTask(*task, task->pendingArgs);
    return 0;
}

int LuauEngine::task_delay(lua_State* L) {
    // L[1]=秒, L[2]=fn, L[3..]=引数。初回resumeはupdate()でdelay経過後
    float sec = (float)luaL_checknumber(L, 1);
    createEngineTask(L, 2, sec);
    return 0;
}

void LuauEngine::resumeEngineTask(EngineTask& task, int nargs) {
    if (!task.co || task.finished) return;

    // タスク内のwait()がcurrentScriptに誤って記録されないよう、実行文脈を切り替える
    Script*     savedScript = currentScript;
    EngineTask* savedTask   = currentTask;
    currentScript = nullptr;
    currentTask   = &task;
    task.started  = true;

    FPUState fpuState = saveFPU();
    m_scriptResumeStart = std::chrono::steady_clock::now();
    beginProtectedExecution();
    int result = lua_resume(task.co, L, nargs);
    restoreFPU(fpuState);

    currentTask   = savedTask;
    currentScript = savedScript;

    if (result == LUA_YIELD) {
        // wait()由来ならupdateEngineTasksが再開する。それ以外のyield(素の
        // coroutine.yield等)は再開手段が無いため完了扱いで破棄する
        if (!task.sleeping && !task.waitingForPath) task.finished = true;
        return;
    }

    // 完了またはエラー
    task.finished = true;
    if (result != 0) {
        lua_State* errState = (lua_gettop(task.co) > 0) ? task.co : L;
        reportProtectedError(errState, "Task | " + task.sourceLabel);
    }
}

void LuauEngine::updateEngineTasks(float deltaTime) {
    // このフレーム中にresume内から追加されたタスクは次フレームから処理する
    size_t count = m_tasks.size();
    for (size_t i = 0; i < count; ++i) {
        EngineTask* t = m_tasks[i].get();
        if (t->finished) continue;
        if (!t->started) {
            t->delayRemaining -= deltaTime;
            if (t->delayRemaining <= 0.0f) {
                resumeEngineTask(*t, t->pendingArgs);
            }
        } else if (t->sleeping) {
            t->sleepRemaining -= deltaTime;
            if (t->sleepRemaining <= 0.0f) {
                t->sleeping = false;
                resumeEngineTask(*t, 0);
            }
        }
    }

    // 完了タスクの掃除(レジストリ参照を解放してGCに任せる)
    m_tasks.erase(std::remove_if(m_tasks.begin(), m_tasks.end(),
        [this](const std::unique_ptr<EngineTask>& t) {
            if (!t->finished) return false;
            if (t->coRef != -1) lua_unref(L, t->coRef);
            return true;
        }), m_tasks.end());
}

void LuauEngine::cancelAllTasks() {
    for (auto& pending : m_pendingPaths) {
        if (!pending) continue;
        if (auto service = pending->service.lock())
            service->AbandonRequest(pending->requestId);
        if (pending->owner == PathCoroutineOwner::Script) {
            if (auto script = pending->script.lock()) {
                script->WaitingForPath = false;
                if (script->Coroutine == pending->co) {
                    if (script->CoroutineRef != -1)
                        lua_unref(L, script->CoroutineRef);
                    script->CoroutineRef = -1;
                    script->Coroutine = nullptr;
                }
            }
        } else if (pending->owner == PathCoroutineOwner::EngineTask && pending->task) {
            pending->task->waitingForPath = false;
        }
        if (pending->coRef != -1) lua_unref(L, pending->coRef);
    }
    m_pendingPaths.clear();
    for (auto& t : m_tasks) {
        if (t->coRef != -1) lua_unref(L, t->coRef);
    }
    m_tasks.clear();
}

// requireのモジュールキャッシュ(Luaレジストリのテーブル名)と、
// 読み込み中を示すsentinel(このstatic変数のアドレスをlightuserdataとして使う)
static constexpr const char* RCBN_MODULE_CACHE_KEY = "RCBN_ModuleCache";
static const char s_moduleLoadingSentinel = 0;

// require(moduleScript) — ModuleScriptを同期実行し、返した値をキャッシュして返す。
// 2回目以降は同一の値を返す。モジュール本体でのyield(wait等)は不可。
int LuauEngine::luafn_require(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)luaL_checkudata(L, 1, RCBN_INST_METATABLE);
    auto inst = ud->lock();
    if (!inst || !inst->IsA("ModuleScript")) {
        luaL_error(L, "require: argument must be a ModuleScript");
        return 0; // 到達しない（luaL_errorはlongjmp）
    }
    auto* module = static_cast<Script*>(inst.get());

    auto* engine = static_cast<LuauEngine*>(lua_callbacks(L)->userdata);
    if (!engine) { lua_pushnil(L); return 1; }

    // キャッシュテーブルを取得（無ければ作成）。レジストリは全コルーチンで共有される
    lua_getfield(L, LUA_REGISTRYINDEX, RCBN_MODULE_CACHE_KEY);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, RCBN_MODULE_CACHE_KEY);
    }

    // キャッシュヒット判定（key: Instance*のlightuserdata）
    lua_pushlightuserdata(L, inst.get());
    lua_rawget(L, -2); // [cache, value]
    if (!lua_isnil(L, -1)) {
        if (lua_touserdata(L, -1) == (void*)&s_moduleLoadingSentinel) {
            luaL_error(L, "require: cyclic require detected for '%s'", module->Name.c_str());
        }
        lua_remove(L, -2); // [value]
        return 1;
    }
    lua_pop(L, 1); // [cache]

    // 読み込み開始をマークして循環requireを検出可能にする
    lua_pushlightuserdata(L, inst.get());
    lua_pushlightuserdata(L, (void*)&s_moduleLoadingSentinel);
    lua_rawset(L, -3); // [cache]

    // sentinelをキャッシュから取り除く（エラー経路の共通処理）
    auto clearSentinel = [&](int cacheIdx) {
        lua_pushlightuserdata(L, inst.get());
        lua_pushnil(L);
        lua_rawset(L, cacheIdx - 2); // push 2つ分だけcacheが深くなる
    };

    if (!engine->loadScriptChunk(L, *module)) { // 成功時 [cache, chunk]
        clearSentinel(-1); // 失敗時は何も積まれていない [cache]
        luaL_error(L, "require: failed to load module '%s'", module->Name.c_str());
    }

    // 呼び出し元スレッド上で同期実行（1個の返り値を要求）
    engine->beginProtectedExecution();
    if (lua_pcall(L, 0, 1, 0) != 0) { // [cache, err]
        std::string errMsg = engine->consumeProtectedError(L); // [cache]
        // The outer Script/Task protected call adds its own traceback. Keep the
        // rethrown module error to one line to avoid duplicating caller frames.
        if (size_t newline = errMsg.find('\n'); newline != std::string::npos)
            errMsg.resize(newline);
        clearSentinel(-1);
        luaL_error(L, "require: error in module '%s': %s", module->Name.c_str(), errMsg.c_str());
    }

    // [cache, result]
    if (lua_isnil(L, -1)) {
        clearSentinel(-2);
        luaL_error(L, "require: module '%s' must return a value", module->Name.c_str());
    }

    // キャッシュに保存して結果だけ残す
    lua_pushlightuserdata(L, inst.get()); // [cache, result, key]
    lua_pushvalue(L, -2);                 // [cache, result, key, result]
    lua_rawset(L, -4);                    // [cache, result]
    lua_remove(L, -2);                    // [result]
    return 1;
}

void LuauEngine::clearModuleCache() {
    lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, RCBN_MODULE_CACHE_KEY);
}

int LuauEngine::global_print_message(lua_State* L) {
    int n = lua_gettop(L);
    std::ostringstream ss;
    for (int i = 1; i <= n; i++) {
        if (i > 1) ss << "\t";
        if (isTableAt(L, i)) {
            std::vector<const void*> ancestors;
            ss << formatTable(L, i, lua_upvalueindex(1), ancestors);
        } else {
            ss << luaL_tolstring(L, i, nullptr);
            lua_pop(L, 1);
        }
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
    // RCBN_LOG(key);
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

static bool skipScriptForNetworkContext(const std::shared_ptr<Script>& script,
                                        const System* system) {
    if (!script || !system || !system->UseNetwork) return false;
    auto& network = NetworkManager::get();
    if (network.getRole() == NetworkRole::Client)
        return !script->IsA("LocalScript");
    if (network.getRole() == NetworkRole::Host && !network.isLocalPlayer())
        return script->IsA("LocalScript");
    return false;
}

void LuauEngine::executeWorkspaceScripts(Workspace& ws) {
    if (m_haltRequested) return; // 安全対策による強制停止済み

    // このWorkspaceのスクリプトが実行される前に workspace グローバルを切り替える
    auto wsSp = std::static_pointer_cast<Workspace>(ws.shared_from_this());
    auto* ud = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
    new (ud) std::weak_ptr<Instance>(wsSp);
    luaL_getmetatable(L, RCBN_INST_METATABLE);
    lua_setmetatable(L, -2);
    lua_setglobal(L, "workspace");

    for (auto& inst : ws.scripts) {
        auto script = std::dynamic_pointer_cast<Script>(inst);
        if (skipScriptForNetworkContext(script, m_system)) continue;
        if (script && script->Enabled && !script->Sleeping && !script->WaitingForChild
            && !script->WaitingForPath && !script->Completed && !script->Aborted) {
            execute(*script);
        }
    }
}

void LuauEngine::executeSystemScripts() {
    if (m_haltRequested || !m_system) return;

    // 実行中のスクリプトが親付け替え等で登録リストを変更しても安全なようコピーして回す
    auto scripts = m_system->scripts;
    for (auto& inst : scripts) {
        auto script = std::dynamic_pointer_cast<Script>(inst);
        if (skipScriptForNetworkContext(script, m_system)) continue;
        if (script && script->Enabled && !script->Sleeping && !script->WaitingForChild
            && !script->WaitingForPath && !script->Completed && !script->Aborted) {
            execute(*script);
        }
    }
}

void LuauEngine::resetSystemScripts() {
    if (!m_system) return;
    for (auto& inst : m_system->scripts) {
        auto script = std::dynamic_pointer_cast<Script>(inst);
        if (!script) continue;
        // restart()はCoroutineRefのunref済みを前提とする(Script.cppのコメント参照)
        if (script->CoroutineRef != -1) lua_unref(L, script->CoroutineRef);
        script->restart();
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

int LuauEngine::instance_wait_child_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto obj = ud->lock();
    if (!obj) { lua_pushnil(L); return 1; }

    // L[1] = self, L[2] = 子名, L[3] = 任意 timeout(秒)
    const char* name = luaL_checkstring(L, 2);

    // 既に存在すれば yield せず即返す
    Instance* child = obj->getChild(name ? name : "");
    if (child) {
        auto* cud = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
        new (cud) std::weak_ptr<Instance>(child->shared_from_this());
        luaL_getmetatable(L, RCBN_INST_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    }

    // スクリプト文脈が無い（コルーチン外）なら待機できないので nil を返す
    if (currentScript == nullptr) { lua_pushnil(L); return 1; }

    float timeout = -1.0f;
    if (lua_gettop(L) >= 3 && lua_isnumber(L, 3)) timeout = (float)lua_tonumber(L, 3);

    currentScript->WaitingForChild = true;
    currentScript->WaitTarget      = obj;
    currentScript->WaitChildName   = name ? name : "";
    currentScript->WaitTimeout     = timeout;
    currentScript->WaitElapsed     = 0.0f;

    // 子が出現するまで（または timeout まで）コルーチンを停止。
    // 再開は update() → resumeWaitChild() が値を1つ渡して行う。
    return lua_yield(L, 0);
}

void LuauEngine::pollWaitChild(Script& script, float deltaTime) {
    script.WaitElapsed += deltaTime;
    auto target = script.WaitTarget.lock();
    if (!target) { resumeWaitChild(script, nullptr); return; } // 対象が消滅 → nil で打ち切り

    Instance* child = target->getChild(script.WaitChildName);
    if (child) {
        resumeWaitChild(script, child);
    } else if (script.WaitTimeout >= 0.0f && script.WaitElapsed >= script.WaitTimeout) {
        resumeWaitChild(script, nullptr); // タイムアウト → nil
    }
}

void LuauEngine::resumeWaitChild(Script& script, Instance* childOrNull) {
    lua_State* co = script.Coroutine;
    script.WaitingForChild = false;
    if (!co) return;

    setWorkspaceGlobal(co, resolveScriptWorkspace(script));
    currentScript = &script;

    // yield の戻り値として、見つかった子（無ければ nil）を1つ積んでから再開する
    if (childOrNull) {
        auto* cud = (std::weak_ptr<Instance>*)lua_newuserdata(co, sizeof(std::weak_ptr<Instance>));
        new (cud) std::weak_ptr<Instance>(childOrNull->shared_from_this());
        luaL_getmetatable(co, RCBN_INST_METATABLE);
        lua_setmetatable(co, -2);
    } else {
        lua_pushnil(co);
    }

    FPUState fpuState = saveFPU();
    m_scriptResumeStart = std::chrono::steady_clock::now();
    beginProtectedExecution();
    int result = lua_resume(co, L, 1);
    restoreFPU(fpuState);

    if (result == LUA_YIELD) {
        currentScript = nullptr;
        return;
    } else if (script.Coroutine != co) {
        // 実行中に自分自身へRestart()が呼ばれ、Coroutineが差し替え済み。
        // このcoの完了/エラー結果でrestart()後の新しい状態を上書きせず、
        // execute()のループへ委ねて同一フレーム内で即座に新しい実行を続ける。
        currentScript = nullptr;
        execute(script);
        return;
    } else if (result == 0) {
        script.Sleeping  = false;
        script.Completed = true;
        lua_unref(L, script.CoroutineRef);
        script.CoroutineRef = -1;
        script.Coroutine = nullptr;
        currentScript    = nullptr;
        return;
    } else {
        script.Aborted = true;
        lua_State* errState = (lua_gettop(co) > 0) ? co : L;
        reportProtectedError(errState, "Script | " + scriptExecutionLabel(&script));
        lua_unref(L, script.CoroutineRef);
        script.CoroutineRef = -1;
        script.Coroutine = nullptr;
        currentScript = nullptr;
        return;
    }
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
    // s_ownedInstances が保持していた強参照も手放す（未親付けのまま Destroy されたケースの解放）
    auto& v = s_ownedInstances;
    v.erase(std::remove(v.begin(), v.end(), obj), v.end());
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

int LuauEngine::sound_reset_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto obj = ud->lock();
    if (obj) static_cast<Sound*>(obj.get())->reset();
    return 0;
}

int LuauEngine::sound_seek_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto obj = ud->lock();
    if (!obj) return 0;
    // L[1] = self, L[2] = シーク秒
    float sec = static_cast<float>(luaL_checknumber(L, 2));
    static_cast<Sound*>(obj.get())->seekSeconds(sec);
    return 0;
}

int LuauEngine::particle_emitter_emit_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto obj = ud->lock();
    if (!obj) return 0;
    // L[1] = self, L[2] = 発生数（省略時1）
    int count = static_cast<int>(luaL_optinteger(L, 2, 1));
    static_cast<ParticleEmitter*>(obj.get())->emit(count);
    return 0;
}

int LuauEngine::humanoid_play_animation_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto self = ud->lock();
    if (!self) return 0;

    // L[1] = self, L[2] = Animation インスタンス
    auto* animUd = (std::weak_ptr<Instance>*)luaL_checkudata(L, 2, RCBN_INST_METATABLE);
    auto anim = std::dynamic_pointer_cast<Animation>(animUd->lock());
    if (anim) static_cast<Humanoid*>(self.get())->playAnimation(anim);
    return 0;
}

int LuauEngine::humanoid_pause_animation_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto self = ud->lock();
    if (self) static_cast<Humanoid*>(self.get())->pauseAnimation();
    return 0;
}

int LuauEngine::humanoid_stop_animation_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto self = ud->lock();
    if (self) static_cast<Humanoid*>(self.get())->stopAnimation();
    return 0;
}

int LuauEngine::humanoid_take_damage_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto self = ud->lock();
    if (!self) return 0;
    // L[1] = self, L[2] = ダメージ量
    float n = static_cast<float>(luaL_checknumber(L, 2));
    static_cast<Humanoid*>(self.get())->takeDamage(n);
    return 0;
}

int LuauEngine::humanoid_move_toward_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto self = ud->lock();
    if (!self) { lua_pushboolean(L, 0); return 1; }

    // L[1] = self, L[2] = 目標地点(ワールド座標)
    Vector3* target = (Vector3*)luaL_checkudata(L, 2, RCBN_VEC3_METATABLE);

    Instance* wsInst = self->findFirstAncestorWorkspace();
    Physics* physics = wsInst ? static_cast<Workspace*>(wsInst)->getPhysicsEngine() : nullptr;

    bool arrived = static_cast<Humanoid*>(self.get())->moveToward(
        *target, physics, SystemState::get().deltaTime);
    lua_pushboolean(L, arrived ? 1 : 0);
    return 1;
}

int LuauEngine::humanoid_jump_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto self = ud->lock();
    if (self) {
        Instance* wsInst = self->findFirstAncestorWorkspace();
        Physics* physics = wsInst ? static_cast<Workspace*>(wsInst)->getPhysicsEngine() : nullptr;
        static_cast<Humanoid*>(self.get())->jump(physics);
    }
    return 0;
}

// L[argIdx] が数値(1始まりスロット) か 文字列(Tool名) を 0始まりスロット index に解決する。無効なら -1。
static int resolveUserSlotArg(lua_State* L, User* user, int argIdx) {
    if (lua_isnumber(L, argIdx)) {
        return (int)lua_tointeger(L, argIdx) - 1; // 1始まり → 0始まり
    }
    if (lua_isstring(L, argIdx)) {
        return user->findSlotByName(lua_tostring(L, argIdx));
    }
    return -1;
}

// Tool（Instance）を weak_ptr userdata として L に積む
static void pushInstanceUserdata(lua_State* L, const std::shared_ptr<Instance>& inst) {
    auto* ud = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
    new (ud) std::weak_ptr<Instance>(inst);
    luaL_getmetatable(L, LuauEngine::RCBN_INST_METATABLE);
    lua_setmetatable(L, -2);
}

void LuauEngine::pushInstance(lua_State* L, const std::shared_ptr<Instance>& inst) {
    pushInstanceUserdata(L, inst);
}

int LuauEngine::user_add_tool_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto self = ud->lock();
    if (!self) { lua_pushnil(L); return 1; }
    User* user = static_cast<User*>(self.get());

    // L[1] = self(User), L[2] = Tool, L[3] = 任意スロット番号(1始まり)
    auto* tud = (std::weak_ptr<Instance>*)luaL_checkudata(L, 2, RCBN_INST_METATABLE);
    auto toolInst = tud->lock();
    if (!toolInst || !toolInst->IsA("Tool")) {
        luaL_error(L, "AddTool: argument #1 must be a Tool");
        return 0; // 到達しない（luaL_error は longjmp する）
    }
    auto tool = std::static_pointer_cast<Tool>(toolInst);

    int slot = -1; // 省略時は空きスロット
    if (lua_gettop(L) >= 3 && lua_isnumber(L, 3)) {
        slot = (int)lua_tointeger(L, 3) - 1; // 1始まり → 0始まり
    }

    int used = user->addToolToSlot(tool, slot);
    if (used < 0) {
        // サイレント失敗を防ぐ: スロット満杯/範囲外はnilを返しつつ警告を出す
        RCBN_WARN("AddTool: no free slot for tool '" << tool->Name << "' (slots full or invalid index)");
        lua_pushnil(L);
    } else {
        lua_pushinteger(L, used + 1); // 1始まりで返す
    }
    return 1;
}

int LuauEngine::user_remove_tool_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto self = ud->lock();
    if (!self) { lua_pushnil(L); return 1; }
    User* user = static_cast<User*>(self.get());

    // L[2] = スロット番号(1始まり) または Tool 名(string)
    int slot = resolveUserSlotArg(L, user, 2);
    auto tool = user->removeToolFromSlot(slot);
    if (!tool) { lua_pushnil(L); return 1; }

    // ツリーからデタッチ済み。Luau が参照を保持できるよう所有権を retain してから返す
    s_ownedInstances.push_back(std::static_pointer_cast<Instance>(tool));
    pushInstanceUserdata(L, std::static_pointer_cast<Instance>(tool));
    return 1;
}

int LuauEngine::user_get_tool_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto self = ud->lock();
    if (!self) { lua_pushnil(L); return 1; }
    User* user = static_cast<User*>(self.get());

    // L[2] = スロット番号(1始まり) または Tool 名(string)
    int slot = resolveUserSlotArg(L, user, 2);
    auto tool = user->getToolInSlot(slot);
    if (!tool) { lua_pushnil(L); return 1; }
    pushInstanceUserdata(L, std::static_pointer_cast<Instance>(tool));
    return 1;
}

int LuauEngine::user_get_tools_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto self = ud->lock();
    lua_newtable(L);
    if (!self) return 1;
    User* user = static_cast<User*>(self.get());

    int idx = 1;
    for (auto& tool : user->Slots) {
        if (!tool) continue; // 空きスロットは詰めて連番にする（ipairs 用）
        pushInstanceUserdata(L, std::static_pointer_cast<Instance>(tool));
        lua_rawseti(L, -2, idx++);
    }
    return 1;
}

int LuauEngine::user_get_mouse_ray_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto self = ud->lock();
    if (!self) { lua_pushnil(L); return 1; }
    User* user = static_cast<User*>(self.get());

    // ビューポート未記録
    if (user->m_gameVpW <= 0.f || user->m_gameVpH <= 0.f) { lua_pushnil(L); return 1; }

    float screenX, screenY;
    if (user->isRotatingCamera()) {
        // ドラッグ中はOSカーソルが毎フレームアンカーへ戻される(User::processCameraRotation)ため、
        // ImGui::GetMousePos()ではなくアンカー位置(=見た目上固定表示される仮想マウス位置)を使う
        double ax, ay;
        user->getRotationAnchor(ax, ay);
        // getRotationAnchorはウィンドウクライアント座標。マルチビューポート有効時、m_gameVpX/Yは
        // デスクトップ絶対座標になるため、drawCameraRotationCursor(Renderer_GUI.cpp)と同様に
        // メインビューポート位置を加算する
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImVec2 mainPos = ImGui::GetMainViewport()->Pos;
            ax += mainPos.x;
            ay += mainPos.y;
        }
        screenX = static_cast<float>(ax);
        screenY = static_cast<float>(ay);
    } else {
        ImVec2 mousePos = ImGui::GetMousePos();
        screenX = mousePos.x;
        screenY = mousePos.y;
    }
    float mx = screenX - user->m_gameVpX, my = screenY - user->m_gameVpY;
    if (mx < 0.f || my < 0.f || mx > user->m_gameVpW || my > user->m_gameVpH) { lua_pushnil(L); return 1; }

    float ndcX = (mx / user->m_gameVpW) * 2.0f - 1.0f;
    float ndcY = 1.0f - (my / user->m_gameVpH) * 2.0f;
    float aspect = user->m_gameProjectionAspect > 0.f
        ? user->m_gameProjectionAspect
        : user->m_gameVpW / user->m_gameVpH;
    float tanH = std::tan(45.0f * (3.14159265f / 180.0f) * 0.5f);
    Vector3 dir = (user->m_gameCameraForward
                 + user->m_gameCameraRight * (ndcX * aspect * tanH)
                 + user->m_gameCameraUp    * (ndcY * tanH)).normalize();

    lua_newtable(L);

    Vector3* origin = (Vector3*)lua_newuserdata(L, sizeof(Vector3));
    *origin = user->m_gameCameraPosition;
    luaL_getmetatable(L, RCBN_VEC3_METATABLE);
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, "Origin");

    Vector3* dirUd = (Vector3*)lua_newuserdata(L, sizeof(Vector3));
    *dirUd = dir;
    luaL_getmetatable(L, RCBN_VEC3_METATABLE);
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, "Direction");

    return 1;
}

int LuauEngine::userinput_ispressed_closure(lua_State* L) {
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto self = ud->lock();
    if (!self) { lua_pushboolean(L, 0); return 1; }

    // L[1] = self, L[2] = キー名(string)
    const char* key = luaL_checkstring(L, 2);
    bool pressed = static_cast<UserInput*>(self.get())->isPressed(key ? key : "");
    lua_pushboolean(L, pressed ? 1 : 0);
    return 1;
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

void LuauEngine::pushQuaternion(lua_State* L, Quaternion q) {
    auto* ud = (Quaternion*)lua_newuserdata(L, sizeof(Quaternion));
    *ud = q;
    luaL_getmetatable(L, RCBN_QUATERNION_METATABLE);
    lua_setmetatable(L, -2);
}

void LuauEngine::pushCFrame(lua_State* L, CFrame cf) {
    auto* ud = (CFrame*)lua_newuserdata(L, sizeof(CFrame));
    *ud = cf;
    luaL_getmetatable(L, RCBN_CFRAME_METATABLE);
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
    std::string sourceLabel = currentScript ? scriptExecutionLabel(currentScript)
                                            : (currentTask ? currentTask->sourceLabel : "Unknown signal listener");
    int id = sig->connect(L, ref, false, std::move(sourceLabel));
    pushConnection(L, std::make_shared<RCBNScriptConnection>(shared, id));
    return 1;
}

int LuauEngine::signal_once_closure(lua_State* L) {
    auto* sig = static_cast<RCBNScriptSignal*>(lua_touserdata(L, lua_upvalueindex(1)));
    // arg1 = self (signal), arg2 = callback function
    int ref = lua_ref(L, 2);
    auto shared = sig->shared_from_this();
    std::string sourceLabel = currentScript ? scriptExecutionLabel(currentScript)
                                            : (currentTask ? currentTask->sourceLabel : "Unknown signal listener");
    int id = sig->connect(L, ref, true, std::move(sourceLabel));
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
    std::string sourceLabel = currentScript ? scriptExecutionLabel(currentScript)
                                            : (currentTask ? currentTask->sourceLabel : "Unknown signal listener");
    int id = sig->connect(L, ref, false, std::move(sourceLabel));
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
// クラス名 -> ファクトリ関数の対応表。新しいクラスを追加する場合はここに1行足すだけでよい。
// System はルートの唯一無二インスタンスのため、複製生成を避ける目的で意図的に対象外にしている。
static const std::unordered_map<std::string, std::function<std::shared_ptr<Instance>()>>& instanceFactories() {
    static const std::unordered_map<std::string, std::function<std::shared_ptr<Instance>()>> table = {
        { "Instance",         [] { return std::make_shared<Instance>("Instance"); } },
        { "Folder",           [] { return std::make_shared<Folder>(); } },
        { "Workspace",        [] { return std::make_shared<Workspace>(); } },
        { "PathfindingService", [] { return std::make_shared<PathfindingService>(); } },
        { "Script",           [] { return std::make_shared<Script>(""); } },
        { "LocalScript",      [] { return std::make_shared<LocalScript>(""); } },
        { "ModuleScript",     [] { return std::make_shared<ModuleScript>(""); } },
        { "Model",            [] { return std::make_shared<Model>(); } },
        { "Decal",            [] { return std::make_shared<Decal>(0, Face::Front); } },
        { "Texture",          [] { return std::make_shared<Texture>(0, Face::Front); } },
        { "Canvas",           [] { return std::make_shared<Canvas>(); } },
        { "Sound",            [] () -> std::shared_ptr<Instance> {
              return AudioService::instance ? std::make_shared<Sound>(*AudioService::instance) : nullptr;
          } },
        { "Lighting",         [] { return std::make_shared<Lighting>(); } },
        { "PointLight",       [] { return std::make_shared<PointLight>(); } },
        { "SpotLight",        [] { return std::make_shared<SpotLight>(); } },
        { "PostEffect",       [] { return std::make_shared<PostEffect>(); } },
        { "AppImage",         [] { return std::make_shared<AppImage>(); } },
        { "Humanoid",         [] { return std::make_shared<Humanoid>(); } },
        { "Animation",        [] { return std::make_shared<Animation>(); } },
        { "StarterCharacter", [] { return std::make_shared<StarterCharacter>(); } },
        { "Terrain",          [] { return std::make_shared<Terrain>(); } },
        { "Rope",             [] { return std::make_shared<Rope>(); } },
        { "Rod",              [] { return std::make_shared<Rod>(); } },
        { "BallSocket",       [] { return std::make_shared<BallSocket>(); } },
        { "NoCollision",      [] { return std::make_shared<NoCollision>(); } },
        { "IntValue",         [] { return std::make_shared<IntValue>(); } },
        { "BoolValue",        [] { return std::make_shared<BoolValue>(); } },
        { "NumberValue",      [] { return std::make_shared<NumberValue>(); } },
        { "Vector3Value",     [] { return std::make_shared<Vector3Value>(); } },
        { "Color4Value",      [] { return std::make_shared<Color4Value>(); } },
        { "CFrameValue",      [] { return std::make_shared<CFrameValue>(); } },
        { "QuaternionValue",  [] { return std::make_shared<QuaternionValue>(); } },
        { "ObjectValue",      [] { return std::make_shared<ObjectValue>(); } },
        { "Weld",             [] { return std::make_shared<Weld>(); } },
        { "Motor",            [] { return std::make_shared<Motor>(); } },
        { "Attachment",       [] { return std::make_shared<Attachment>(); } },
        { "Force",            [] { return std::make_shared<Force>(); } },
        { "TextLabel",        [] { return std::make_shared<TextLabel>(); } },
        { "TextButton",       [] { return std::make_shared<TextButton>(); } },
        { "ImageLabel",       [] { return std::make_shared<ImageLabel>(); } },
        { "ImageButton",      [] { return std::make_shared<ImageButton>(); } },
        { "SurfaceGui",       [] { return std::make_shared<SurfaceGui>(); } },
        { "BillboardGui",     [] { return std::make_shared<BillboardGui>(); } },
        { "ProximityPrompt",  [] { return std::make_shared<ProximityPrompt>(); } },
        { "Tool",             [] { return std::make_shared<Tool>("Tool"); } },
        { "Event",            [] { return std::make_shared<Event>(); } },
        { "SignalEvent",       [] { return std::make_shared<SignalEvent>(); } },
        { "ParticleEmitter",  [] { return std::make_shared<ParticleEmitter>(); } },
        { "Weather",          [] { return std::make_shared<Weather>(); } },
    };
    return table;
}

int LuauEngine::instance_new_closure(lua_State* L) {
    const char* className = luaL_checkstring(L, 1);

    std::shared_ptr<Instance> inst = createBaseCubeInstance(className);
    if (!inst) inst = PhysicalFileInstanceRegistry::create(className);
    if (!inst) {
        auto it = instanceFactories().find(className);
        if (it != instanceFactories().end()) inst = it->second();
    }

    if (!inst) { lua_pushnil(L); return 1; }

    s_ownedInstances.push_back(inst);
    auto* ud = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
    new (ud) std::weak_ptr<Instance>(inst);
    luaL_getmetatable(L, RCBN_INST_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

// instance:Clone() — サブツリーを複製し（制約参照も張り替え）、親なしで返す。
// Roblox 同様、戻り値の .Parent を設定するまではツリーに入らない。
// s_ownedInstances が shared_ptr を保持し、Lua の weak_ptr だけでも GC されないようにする。
int LuauEngine::instance_clone_closure(lua_State* L) {
    auto* userdata = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto obj_shared = userdata->lock();
    if (!obj_shared) { lua_pushnil(L); return 1; }

    auto copy = obj_shared->cloneTree();
    if (!copy) { lua_pushnil(L); return 1; }

    s_ownedInstances.push_back(copy);
    auto* ud = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
    new (ud) std::weak_ptr<Instance>(copy);
    luaL_getmetatable(L, RCBN_INST_METATABLE);
    lua_setmetatable(L, -2);

    auto* engine = static_cast<LuauEngine*>(lua_callbacks(L)->userdata);
    if (engine && engine->m_system) {
        std::string label = scriptSafetyLabel(currentScript);
        int total = ++engine->m_totalClonesThisFrame;
        engine->m_cloneCallCounts[label]++;
        if (total > engine->m_system->MaxClonesPerFrame) {
            engine->reportSafetyBreach("Infinite cloning possible", engine->m_cloneCallCounts);
            luaL_error(L, "Safety limit exceeded: too many Instance:Clone() calls this frame (max %d)",
                       engine->m_system->MaxClonesPerFrame);
        }
    }
    return 1;
}

// ===================================================
//  Script:Restart クロージャー
// ===================================================
int LuauEngine::script_restart_closure(lua_State* L) {
    auto* userdata = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    auto obj_shared = userdata->lock();
    if (!obj_shared) return 0;
    auto* script = static_cast<Script*>(obj_shared.get());

    auto* engine = static_cast<LuauEngine*>(lua_callbacks(L)->userdata);
    if (engine && engine->m_system) {
        std::string label = scriptSafetyLabel(currentScript);
        int total = ++engine->m_totalRestartsThisFrame;
        engine->m_restartCallCounts[label]++;
        if (total > engine->m_system->MaxRestartsPerFrame) {
            engine->reportSafetyBreach("Infinity recursion possible", engine->m_restartCallCounts);
            luaL_error(L, "Safety limit exceeded: too many Script:Restart() calls this frame (max %d)",
                       engine->m_system->MaxRestartsPerFrame);
            return 0; // 到達しない（luaL_errorはlongjmp）
        }
    }

    bool isSelfRestart = (script == currentScript);

    if (isSelfRestart) {
        // 自分自身の再起動: ここではexecute()を再帰呼びしない。
        // Coroutineを差し替えてluaL_errorで古い実行を打ち切れば、その巻き戻し先である
        // execute()自身のループ(script.Coroutine != co の分岐)が同一フレーム内で
        // 即座に新しいコルーチンを実行してくれる（C++スタックを伸ばさない）。
        if (script->CoroutineRef != -1) lua_unref(L, script->CoroutineRef);
        script->restart();
        luaL_error(L, "Script restarted"); // Coroutine差し替え済みなのでAbortedへは誤変換されない
        return 0; // 到達しない
    }

    // 他スクリプトの再起動: 対象は別コルーチンなので同期的に再帰実行してから
    // 呼び出し元の続きを実行する（対象自身の自己Restart()連鎖はexecute()内のループで吸収される）。
    Script* savedCurrent = currentScript;
    if (script->CoroutineRef != -1) lua_unref(L, script->CoroutineRef);
    script->restart();
    if (engine) engine->execute(*script);
    currentScript = savedCurrent; // 再帰実行でnullptrに戻された呼び出し元の文脈を復元
    return 0;
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
//  SignalEvent:Fire クロージャー
// ===================================================
int LuauEngine::signalevent_fire_closure(lua_State* L) {
    // upvalue 1 = SignalEvent の weak_ptr userdata
    auto* ud = (std::weak_ptr<Instance>*)lua_touserdata(L, lua_upvalueindex(1));
    if (!ud) return 0;
    auto self = ud->lock();
    if (!self) return 0;
    if (!self->IsA("SignalEvent")) return 0;
    auto* se = static_cast<SignalEvent*>(self.get());
    if (!se->Fired) return 0;

    int top = lua_gettop(L); // L[1]=self, L[2..top]=可変引数
    se->Fired->fire(L, [source = L, top](lua_State* Lx) -> int {
        for (int i = 2; i <= top; ++i) {
            lua_pushvalue(source, i);
            lua_xmove(source, Lx, 1);
        }
        return top - 1;
    });
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

void LuauEngine::fireNetworkRoleChanged(NetworkRole oldRole, NetworkRole newRole) {
    if (!m_system || !m_system->NetworkRoleChanged) return;
    const char* oldStr = NetworkManager::roleToString(oldRole);
    const char* newStr = NetworkManager::roleToString(newRole);
    m_system->NetworkRoleChanged->fire(L, [oldStr, newStr](lua_State* Lx) -> int {
        lua_pushstring(Lx, oldStr);
        lua_pushstring(Lx, newStr);
        return 2;
    });
}

void LuauEngine::fireChatMessage(ChatService* service, PeerId senderId, const std::string& text) {
    if (!service || !service->MessageReceived) return;
    service->MessageReceived->fire(L, [senderId, text](lua_State* state) -> int {
        lua_pushnumber(state, static_cast<double>(senderId));
        lua_pushlstring(state, text.data(), text.size());
        return 2;
    });
}

bool LuauEngine::consumeSafetyHaltRequest() {
    bool v = m_haltRequested;
    m_haltRequested = false;
    return v;
}

void LuauEngine::resetFrameSafetyCounters() {
    m_cloneCallCounts.clear();
    m_restartCallCounts.clear();
    m_taskCallCounts.clear();
    m_totalClonesThisFrame   = 0;
    m_totalRestartsThisFrame = 0;
    m_totalTasksThisFrame    = 0;
}

void LuauEngine::reportSafetyBreach(const std::string& reason,
                                     const std::unordered_map<std::string, int>& counts) {
    std::vector<std::pair<std::string, int>> sorted(counts.begin(), counts.end());
    std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });

    std::string output = "[SAFETY] " + reason + " - halting.\n";
    for (auto& [label, count] : sorted)
        output += "  " + label + ": " + std::to_string(count) + "\n";

    std::cerr << output;
    if (g_luauLogHook) g_luauLogHook("[ERROR] " + output);

    m_haltRequested = true;
}

void LuauEngine::onCollision(BaseCube* a, BaseCube* b) {
    if (!a || !b) return;
    auto aShared = std::dynamic_pointer_cast<BaseCube>(a->shared_from_this());
    auto bShared = std::dynamic_pointer_cast<BaseCube>(b->shared_from_this());
    if (!aShared || !bShared) return;
    std::weak_ptr<BaseCube> weakA = aShared;
    std::weak_ptr<BaseCube> weakB = bShared;
    if (aShared->Touched) {
        aShared->Touched->fire(L, [weakB](lua_State* Lx) -> int {
            auto other = weakB.lock();
            if (!other) { lua_pushnil(Lx); return 1; }
            pushInstanceUserdata(Lx, std::static_pointer_cast<Instance>(other));
            return 1;
        });
    }

    // A側listenerはBをDestroy/reparentできるため、B側通知の直前に
    // weak_ptrとWorkspace所属を取り直す。
    aShared = weakA.lock();
    bShared = weakB.lock();
    if (!aShared || !bShared ||
        !aShared->findFirstAncestorWorkspace() ||
        aShared->findFirstAncestorWorkspace() !=
            bShared->findFirstAncestorWorkspace())
        return;
    if (bShared->Touched) {
        bShared->Touched->fire(L, [weakA](lua_State* Lx) -> int {
            auto other = weakA.lock();
            if (!other) { lua_pushnil(Lx); return 1; }
            pushInstanceUserdata(Lx, std::static_pointer_cast<Instance>(other));
            return 1;
        });
    }
}

void LuauEngine::tickWaitingScript(const std::shared_ptr<Instance>& inst, float deltaTime) {
    auto script = std::dynamic_pointer_cast<Script>(inst);
    if (script && script->Enabled && script->Sleeping && script->Coroutine != nullptr) {
        script->SleepRemaining -= deltaTime;

        // タイムアウト時にコルーチンを再開
        if (script->SleepRemaining <= 0.0f) {
            script->Sleeping = false;
            execute(*script);
        }
    } else if (script && script->Enabled && script->WaitingForChild && script->Coroutine != nullptr) {
        pollWaitChild(*script, deltaTime);
    }
}

void LuauEngine::update(float deltaTime) {
    pollPathfindingRequests();
    if (PathfindingService::IsBuildActive()) return;
    sweepOwnedInstances(); // 毎フレーム、ツリーが所有済み/破棄済みの強参照を手放す
    if (m_haltRequested) return; // 安全対策による強制停止済み

    // task.delay/task.spawnのタスクを進める(m_system分岐は早期returnするため先に行う)
    updateEngineTasks(deltaTime);

    if (m_system) {
        for (auto& [name, child] : m_system->getChildren()) {
            if (!child || !child->IsA("Workspace")) continue;
            auto* ws = static_cast<Workspace*>(child.get());
            auto scripts = ws->scripts;
            for (auto& inst : scripts) {
                tickWaitingScript(inst, deltaTime);
            }
        }
        // Workspace外(System配下)のスクリプトも待機を進める
        auto sysScripts = m_system->scripts;
        for (auto& inst : sysScripts) {
            tickWaitingScript(inst, deltaTime);
        }
        return;
    }

    auto ws = workspace.lock();
    if (!ws) return;

    // 待機中のスクリプトのタイマーを減算
    for (auto& inst : ws->scripts) {
        tickWaitingScript(inst, deltaTime);
    }
}
