#pragma once
#include <iostream>
#include <string>
#include <unordered_map>
#include <cstring>
#include <string_view>
#include <functional>
#include <memory>

#include "include/luau/lua.h"
#include "include/luau/lualib.h"
#include "include/luau/luacode.h"

#include "include/Instances/Instance.hpp"
#include "include/Instances/BaseCube.hpp"
#include "include/Instances/Script.hpp"
#include "include/Math/Vector3.hpp"
#include "include/Math/Vector2.hpp"
#include "include/Math/Quaternion.hpp"
#include "include/Math/CFrame.hpp"
#include "include/Util/Color4.hpp"
#include "include/Core/RCBNScriptSignal.hpp"

// Forward declarations
class Workspace;
class System;
class GuiButton;

#pragma comment(lib, "Luau.VM.lib")
#pragma comment(lib, "Luau.Compiler.lib")
#pragma comment(lib, "Luau.Ast.lib")
#pragma comment(lib, "Luau.Common.lib")

class LuauEngine {
private:
    lua_State* L;
    std::weak_ptr<Workspace> workspace;  // 管理対象の Workspace
    System*    m_system = nullptr;
    static Script* currentScript;  // 現在実行中のスクリプト
    std::string m_lastTraceback;   // debugprotectederror で取得したスタックトレース

    static constexpr const char* ERIK = "erik";

    static constexpr const int NIL = 0;

public:
    static constexpr const char* RCBN_INST_METATABLE       = "RCBN_Instance";
    static constexpr const char* RCBN_OWNED_INST_METATABLE = "RCBN_OwnedInstance";
    static constexpr const char* RCBN_VEC3_METATABLE       = "RCBN_Vector3";
    static constexpr const char* RCBN_COLOR4_METATABLE     = "RCBN_Color4";
    static constexpr const char* RCBN_SIGNAL_METATABLE     = "RCBN_Signal";
    static constexpr const char* RCBN_CONNECTION_METATABLE = "RCBN_Connection";
    static constexpr const char* RCBN_VEC2_METATABLE       = "RCBN_Vector2";
    static constexpr const char* RCBN_QUATERNION_METATABLE = "RCBN_Quaternion";
    static constexpr const char* RCBN_CFRAME_METATABLE     = "RCBN_CFrame";

    using GetterFunc = std::function<int(lua_State*, Instance*)>;
    using SetterFunc = std::function<int(lua_State*, Instance*)>;

private:
    static std::unordered_map<std::string_view, std::unordered_map<std::string_view, GetterFunc>> DispatchTable;
    static std::unordered_map<std::string_view, std::unordered_map<std::string_view, SetterFunc>> SetterTable;

    void InitDispatchTable();
    void InitDispatchTable_Base();     // Instance, BaseCube
    void InitDispatchTable_World();    // Workspace, Decal, Lighting
    void InitDispatchTable_Physics();  // Rope, Rod, Weld, Motor
    void InitDispatchTable_Misc();     // Sound, Humanoid, AppImage, Script
    void InitDispatchTable_GUI();      // ScreenGuiObject, TextLabel, GuiButton, TextButton, WorldGuiObject, SurfaceGui, BillboardGui

    void InitSetterTable();
    void InitSetterTable_Base();
    void InitSetterTable_World();
    void InitSetterTable_Physics();
    void InitSetterTable_Misc();
    void InitSetterTable_GUI();
    void InitMetatables();
    void RegisterGlobalFunctions(lua_State* L);  // コルーチンにも関数を登録するために抽出

    static int instance_index(lua_State* L);
    static int instance_newindex(lua_State* L);
    static int instance_tostring(lua_State* L);
    static int instance_find_child_closure(lua_State* L);
    static int instance_get_children_closure(lua_State* L);
    static int instance_wait_child_closure(lua_State* L);
    static int instance_is_a_closure(lua_State* L);
    static int instance_destroy_closure(lua_State* L);
    static int instance_clone_closure(lua_State* L);

    // User methods
    static int user_add_tool_closure(lua_State* L);
    static int user_remove_tool_closure(lua_State* L);
    static int user_get_tool_closure(lua_State* L);
    static int user_get_tools_closure(lua_State* L);

    // Workspace methods
    static int workspace_raycast_closure(lua_State* L);

    // Sound methods
    static int sound_play_closure(lua_State* L);
    static int sound_stop_closure(lua_State* L);
    static int sound_reset_closure(lua_State* L);
    static int sound_seek_closure(lua_State* L);

    // Humanoid methods（アニメーション再生）
    static int humanoid_play_animation_closure(lua_State* L);
    static int humanoid_pause_animation_closure(lua_State* L);
    static int humanoid_stop_animation_closure(lua_State* L);
    static int humanoid_take_damage_closure(lua_State* L);

    // UserInput methods
    static int userinput_ispressed_closure(lua_State* L);

    // Terrain methods（部分編集 API）
    static int terrain_set_block_closure(lua_State* L);
    static int terrain_remove_block_closure(lua_State* L);
    static int terrain_get_block_closure(lua_State* L);
    static int terrain_raycast_closure(lua_State* L);
    static int terrain_apply_brush_closure(lua_State* L);

    // Vector3 methods
    static int vec3_index(lua_State* L);
    static int vec3_newindex(lua_State* L);
    static int vec3_tostring(lua_State* L);
    static int vec3_constructor(lua_State* L);
    // static int vec3_zeroconstructor(lua_State* L);
    static int vec3_add(lua_State* L);
    static int vec3_sub(lua_State* L);
    static int vec3_unm(lua_State* L);
    static int vec3_mul(lua_State* L);
    static int vec3_div(lua_State* L);
    static int vec3_eq(lua_State* L);

    // Color4 methods
    static int color4_index(lua_State* L);
    static int color4_newindex(lua_State* L);
    static int color4_tostring(lua_State* L);
    static int color4_constructor(lua_State* L);
    static int color4_add(lua_State* L);
    static int color4_sub(lua_State* L);
    static int color4_unm(lua_State* L);
    static int color4_mul(lua_State* L);
    static int color4_div(lua_State* L);
    static int color4_eq(lua_State* L);

    // Vector2 methods
    static int vec2_constructor(lua_State* L);
    static int vec2_index(lua_State* L);
    static int vec2_newindex(lua_State* L);
    static int vec2_tostring(lua_State* L);
    static int vec2_add(lua_State* L);
    static int vec2_sub(lua_State* L);
    static int vec2_unm(lua_State* L);
    static int vec2_mul(lua_State* L);
    static int vec2_div(lua_State* L);
    static int vec2_eq(lua_State* L);

    static void pushVector3(lua_State* L, Vector3 v);
    static void pushColor4(lua_State* L, Color4 c);
    static void pushQuaternion(lua_State* L, Quaternion q);
    static void pushCFrame(lua_State* L, CFrame cf);

    // Quaternion methods
    static int quat_constructor(lua_State* L);
    static int quat_from_euler(lua_State* L);
    static int quat_from_axis_angle(lua_State* L);
    static int quat_slerp(lua_State* L);
    static int quat_index(lua_State* L);
    static int quat_newindex(lua_State* L);
    static int quat_tostring(lua_State* L);
    static int quat_mul(lua_State* L);
    static int quat_eq(lua_State* L);

    // CFrame methods
    static int cframe_constructor(lua_State* L);
    static int cframe_from_axis_angle(lua_State* L);
    static int cframe_index(lua_State* L);
    static int cframe_newindex(lua_State* L);
    static int cframe_tostring(lua_State* L);
    static int cframe_mul(lua_State* L);
    static int cframe_eq(lua_State* L);

    // Global functions
    static int global_add(lua_State* L);
    static int global_print_message(lua_State* L);
    static int wait(lua_State* L);

    static int erik_index(lua_State* L);
    static int erik_tostring(lua_State* L);

    // Signal / Connection
    static int signal_index(lua_State* L);
    static int signal_connect_closure(lua_State* L);
    static int signal_once_closure(lua_State* L);
    static int signal_until_closure(lua_State* L);
    static int connection_index(lua_State* L);
    static int connection_disconnect_closure(lua_State* L);
    static int instance_new_closure(lua_State* L);
    static int event_fire_closure(lua_State* L);

public:
    LuauEngine();
    ~LuauEngine();

    void setBindings(const std::shared_ptr<Instance>& instance);

    void setGlobalInstance(const std::string& name, const std::shared_ptr<Instance>& instance);

    bool execute(Script& script);
    // WaitChild で yield 中のスクリプトを、見つかった子（無ければ nullptr）を結果に渡して再開する
    void resumeWaitChild(Script& script, Instance* childOrNull);
    // WaitChild 待機中のスクリプトを毎フレーム評価し、子の出現/タイムアウトで再開する
    void pollWaitChild(Script& script, float deltaTime);

    void setWorkspace(const std::shared_ptr<Workspace>& ws);
    void setSystem(System* s);

    void executeWorkspaceScripts(Workspace& ws);
    void update(float deltaTime);

    void fireHeartbeat(float dt);
    void onCollision(BaseCube* a, BaseCube* b);

    static void pushSignal(lua_State* L, std::shared_ptr<RCBNScriptSignal> sig);
    static void pushConnection(lua_State* L, std::shared_ptr<RCBNScriptConnection> conn);
    static void pushVector2(lua_State* L, Vector2 v);

    void onGuiButtonActivated(GuiButton* btn);
};
