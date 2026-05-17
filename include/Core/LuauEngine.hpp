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
#include "include/Util/Color4.hpp"
#include "include/Core/RCBNScriptSignal.hpp"

// Forward declarations
class Workspace;
class System;

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

    static constexpr const char* RCBN_INST_METATABLE       = "RCBN_Instance";
    static constexpr const char* RCBN_OWNED_INST_METATABLE = "RCBN_OwnedInstance";
    static constexpr const char* RCBN_VEC3_METATABLE       = "RCBN_Vector3";
    static constexpr const char* RCBN_COLOR4_METATABLE     = "RCBN_Color4";
    static constexpr const char* RCBN_SIGNAL_METATABLE     = "RCBN_Signal";
    static constexpr const char* RCBN_CONNECTION_METATABLE = "RCBN_Connection";

    static constexpr const char* ERIK = "erik";

    static constexpr const int NIL = 0;

public:
    using GetterFunc = std::function<int(lua_State*, Instance*)>;
    using SetterFunc = std::function<int(lua_State*, Instance*)>;

private:
    static std::unordered_map<std::string_view, std::unordered_map<std::string_view, GetterFunc>> DispatchTable;
    static std::unordered_map<std::string_view, std::unordered_map<std::string_view, SetterFunc>> SetterTable;

    void InitDispatchTable();
    void InitDispatchTable_Base();     // Instance, BaseCube
    void InitDispatchTable_World();    // Workspace, Decal, Lighting
    void InitDispatchTable_Physics();  // Rope, Rod, Weld, Motor
    void InitDispatchTable_Misc();     // Sound, CharacterSetting, AppImage, Script

    void InitSetterTable();
    void InitSetterTable_Base();
    void InitSetterTable_World();
    void InitSetterTable_Physics();
    void InitSetterTable_Misc();
    void InitMetatables();
    void RegisterGlobalFunctions(lua_State* L);  // コルーチンにも関数を登録するために抽出

    static int instance_index(lua_State* L);
    static int instance_newindex(lua_State* L);
    static int instance_tostring(lua_State* L);
    static int instance_find_child_closure(lua_State* L);
    static int instance_get_children_closure(lua_State* L);
    static int instance_is_a_closure(lua_State* L);
    static int instance_destroy_closure(lua_State* L);

    // Workspace methods
    static int workspace_raycast_closure(lua_State* L);

    // Sound methods
    static int sound_play_closure(lua_State* L);
    static int sound_stop_closure(lua_State* L);

    // Vector3 methods
    static int vec3_index(lua_State* L);
    static int vec3_newindex(lua_State* L);
    static int vec3_tostring(lua_State* L);
    static int vec3_constructor(lua_State* L);
    // static int vec3_zeroconstructor(lua_State* L);

    // Color4 methods
    static int color4_index(lua_State* L);
    static int color4_newindex(lua_State* L);
    static int color4_tostring(lua_State* L);
    static int color4_constructor(lua_State* L);

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

    void setWorkspace(const std::shared_ptr<Workspace>& ws);
    void setSystem(System* s);

    void executeWorkspaceScripts();
    void update(float deltaTime);

    void fireHeartbeat(float dt);
    void onCollision(BaseCube* a, BaseCube* b);

    static void pushSignal(lua_State* L, std::shared_ptr<RCBNScriptSignal> sig);
    static void pushConnection(lua_State* L, std::shared_ptr<RCBNScriptConnection> conn);
};
