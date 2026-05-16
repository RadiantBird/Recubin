#pragma once
#include <iostream>
#include <string>
#include <unordered_map>
#include <cstring>
#include <string_view>
#include <functional>

#include "include/luau/lua.h"
#include "include/luau/lualib.h"
#include "include/luau/luacode.h"

#include "include/Instances/Instance.hpp"
#include "include/Instances/BaseCube.hpp"
#include "include/Instances/Script.hpp"
#include "include/Math/Vector3.hpp"
#include "include/Util/Color4.hpp"

// Forward declaration
class Workspace;

#pragma comment(lib, "Luau.VM.lib")
#pragma comment(lib, "Luau.Compiler.lib")
#pragma comment(lib, "Luau.Ast.lib")
#pragma comment(lib, "Luau.Common.lib")

class LuauEngine {
private:
    lua_State* L;
    std::weak_ptr<Workspace> workspace;  // 管理対象の Workspace
    static Script* currentScript;  // 現在実行中のスクリプト
    std::string m_lastTraceback;   // debugprotectederror で取得したスタックトレース
    
    static constexpr const char* RCBN_INST_METATABLE = "RCBN_Instance";
    static constexpr const char* RCBN_VEC3_METATABLE = "RCBN_Vector3";
    static constexpr const char* RCBN_COLOR4_METATABLE = "RCBN_Color4";

    static constexpr const char* ERIK = "erik";

    static constexpr const int NIL = 0;

public:
    using GetterFunc = std::function<int(lua_State*, Instance*)>;
    using SetterFunc = std::function<int(lua_State*, Instance*)>;

private:
    static std::unordered_map<std::string_view, std::unordered_map<std::string_view, GetterFunc>> DispatchTable;
    static std::unordered_map<std::string_view, std::unordered_map<std::string_view, SetterFunc>> SetterTable;

    void InitDispatchTable();
    void InitSetterTable();
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

public:
    LuauEngine();
    ~LuauEngine();

    void setBindings(const std::shared_ptr<Instance>& instance);

    void setGlobalInstance(const std::string& name, const std::shared_ptr<Instance>& instance);

    bool execute(Script& script);
    
    // Workspace を設定
    void setWorkspace(const std::shared_ptr<Workspace>& ws);
    
    // Workspace 内のすべてのスクリプトを実行
    void executeWorkspaceScripts();
    
    // メインループから呼び出し - 待機中のスクリプトを再開
    void update(float deltaTime);
};
