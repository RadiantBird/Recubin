#pragma once
#include <iostream>
#include <string>
#include <unordered_map>
#include <cstring>
#include <string_view>
#include <functional>
#include <memory>
#include <chrono>

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
#include "include/Pathfinding/PathTypes.hpp"
#include <Network/NetworkTypes.hpp>
#include "include/Util/RuntimeFileSystem.hpp"

// Forward declarations
class Workspace;
class System;
class GuiButton;
class ChatService;
class PathfindingService;

#pragma comment(lib, "Luau.VM.lib")
#pragma comment(lib, "Luau.Compiler.lib")
#pragma comment(lib, "Luau.Ast.lib")
#pragma comment(lib, "Luau.Common.lib")

class LuauEngine {
private:
    lua_State* L;
    std::weak_ptr<Workspace> workspace;  // 管理対象の Workspace
    System*    m_system = nullptr;
    std::shared_ptr<RuntimeFileSystem> m_runtimeFileSystem;
    static Script* currentScript;  // 現在実行中のスクリプト
    std::string m_lastTraceback;   // debugprotectederror で取得したスタックトレース

    // エンジン管理タスク(task.spawn/task.delay)。Scriptインスタンスに属さない
    // コルーチンの遅延起動・wait()睡眠を管理する
    struct EngineTask {
        lua_State* co        = nullptr;
        int   coRef          = -1;    // GC保護用レジストリ参照
        float delayRemaining = 0.0f;  // 初回resumeまでの残り秒(task.delay)
        int   pendingArgs    = 0;     // 初回resumeで渡す引数の個数(coのスタックに積み済み)
        bool  started        = false; // 初回resume済みか
        bool  sleeping       = false; // task.wait()による睡眠中か
        float sleepRemaining = 0.0f;
        bool  waitingForPath = false; // PathfindingService::FindPathによる待機
        bool  finished       = false; // 掃除待ち(完了/エラー/放置yield)
        std::string sourceLabel;
    };
    std::vector<std::unique_ptr<EngineTask>> m_tasks;
    static EngineTask* currentTask;  // 現在実行中のエンジンタスク(currentScriptと排他)

    enum class PathCoroutineOwner {
        Script,
        EngineTask,
        Signal
    };
    struct PendingPathCoroutine {
        std::weak_ptr<PathfindingService> service;
        uint64_t requestId = 0;
        lua_State* co = nullptr;
        int coRef = -1;
        PathCoroutineOwner owner = PathCoroutineOwner::Signal;
        std::weak_ptr<Script> script;
        EngineTask* task = nullptr;
        std::string context;
    };
    std::vector<std::unique_ptr<PendingPathCoroutine>> m_pendingPaths;

    // ---- 安全対策(1フレームあたりのClone/Restart/Task上限、ループタイムアウト) ----
    std::unordered_map<std::string, int> m_cloneCallCounts;
    std::unordered_map<std::string, int> m_restartCallCounts;
    std::unordered_map<std::string, int> m_taskCallCounts;
    int  m_totalClonesThisFrame   = 0;
    int  m_totalRestartsThisFrame = 0;
    int  m_totalTasksThisFrame    = 0;
    bool m_haltRequested          = false;
    // ループタイムアウト検出用: 直近のlua_resume開始時刻(コルーチンは同時に1つしか
    // 実行されないため、エンジン単位のタイムスタンプ1つで足りる)
    std::chrono::steady_clock::time_point m_scriptResumeStart;
    int m_signalCallbackDepth = 0;

    void reportSafetyBreach(const std::string& reason, const std::unordered_map<std::string, int>& counts);

    static constexpr const char* ERIK = "erik";

    static constexpr const int NIL = 0;

public:
    void beginProtectedExecution();
    std::string consumeProtectedError(lua_State* errorState);
    void reportProtectedError(lua_State* errorState, const std::string& context);
    void reportProtectedMessage(const std::string& context, const std::string& message);
    std::chrono::steady_clock::time_point beginSignalCallback();
    void endSignalCallback(std::chrono::steady_clock::time_point previousStart);

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

    // Scriptが参照すべきworkspace(祖先Workspace、無ければアクティブWorkspace)を返す
    std::shared_ptr<Workspace> resolveScriptWorkspace(Script& script);
    // Sourceのチャンクをco上にロードする(ファイル再読込/.luar/.luauc対応)。
    // 成功時はコンパイル済み関数をcoのスタックに積んでtrueを返す。
    // 失敗時はscript.Abortedを立て、エラーログを出してfalseを返す。
    bool loadScriptChunk(lua_State* co, Script& script);
    // Sleeping/WaitChild状態のスクリプトを1フレーム分進める(update()の共通処理)
    void tickWaitingScript(const std::shared_ptr<Instance>& inst, float deltaTime);

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

    // Runtime file and IPC extension callbacks. The engine is captured as a
    // light-userdata upvalue so the same registration works for every state.
    static int io_read_text(lua_State* L); static int io_read_bytes(lua_State* L);
    static int io_write_text(lua_State* L); static int io_write_bytes(lua_State* L);
    static int io_append_text(lua_State* L); static int io_append_bytes(lua_State* L);
    static int io_exists(lua_State* L); static int io_is_file(lua_State* L);
    static int io_is_directory(lua_State* L); static int io_list(lua_State* L);
    static int io_create_directory(lua_State* L); static int io_copy(lua_State* L);
    static int io_move(lua_State* L); static int io_remove(lua_State* L);
    static int io_remove_tree(lua_State* L);
    static int ipc_connect(lua_State* L); static int ipc_send(lua_State* L);
    static int ipc_receive(lua_State* L); static int ipc_close(lua_State* L);

    // Script methods
    static int script_restart_closure(lua_State* L);

    // User methods
    static int user_add_tool_closure(lua_State* L);
    static int user_remove_tool_closure(lua_State* L);
    static int user_get_tool_closure(lua_State* L);
    static int user_get_tools_closure(lua_State* L);
    static int user_get_mouse_ray_closure(lua_State* L);

    // Workspace methods
    static int workspace_raycast_closure(lua_State* L);

    // Sound methods
    static int sound_play_closure(lua_State* L);
    static int sound_stop_closure(lua_State* L);
    static int sound_reset_closure(lua_State* L);
    static int sound_seek_closure(lua_State* L);

    // ParticleEmitter methods
    static int particle_emitter_emit_closure(lua_State* L);

    // Humanoid methods（アニメーション再生）
    static int humanoid_play_animation_closure(lua_State* L);
    static int humanoid_pause_animation_closure(lua_State* L);
    static int humanoid_stop_animation_closure(lua_State* L);
    static int humanoid_take_damage_closure(lua_State* L);
    static int humanoid_move_toward_closure(lua_State* L);
    static int humanoid_jump_closure(lua_State* L);

    // UserInput methods
    static int userinput_ispressed_closure(lua_State* L);

    // Terrain methods（部分編集 API）
    static int terrain_set_block_closure(lua_State* L);
    static int terrain_remove_block_closure(lua_State* L);
    static int terrain_get_block_closure(lua_State* L);
    static int terrain_raycast_closure(lua_State* L);
    static int terrain_apply_brush_closure(lua_State* L);

    // Canvas methods
    static int canvas_set_pixel_closure(lua_State* L);
    static int canvas_get_pixel_closure(lua_State* L);
    static int canvas_clear_closure(lua_State* L);
    static int canvas_world_to_uv_closure(lua_State* L);

    // PathfindingService methods
    static int pathfinding_find_path_closure(lua_State* L);
    static int pathfinding_configure_closure(lua_State* L);
    static int chat_send_message_closure(lua_State* L);

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
    static int color4_from_rgba(lua_State* L);
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
    static int quat_look_rotation(lua_State* L);
    static int quat_index(lua_State* L);
    static int quat_newindex(lua_State* L);
    static int quat_tostring(lua_State* L);
    static int quat_mul(lua_State* L);
    static int quat_eq(lua_State* L);

    // CFrame methods
    static int cframe_constructor(lua_State* L);
    static int cframe_from_axis_angle(lua_State* L);
    static int cframe_look_at(lua_State* L);
    static int cframe_index(lua_State* L);
    static int cframe_newindex(lua_State* L);
    static int cframe_tostring(lua_State* L);
    static int cframe_mul(lua_State* L);
    static int cframe_eq(lua_State* L);

    // Global functions
    static int global_add(lua_State* L);
    static int luafn_assert(lua_State* L);
    static int global_tostring(lua_State* L);
    static int global_print_message(lua_State* L);
    static int wait(lua_State* L);
    static int luafn_require(lua_State* L);  // require(moduleScript)

    // taskモジュール
    static int task_spawn(lua_State* L);  // task.spawn(fn, ...): 即時に並行実行
    static int task_delay(lua_State* L);  // task.delay(sec, fn, ...): sec秒後に実行
    // task.spawn/task.delay共通のタスク生成(fn+可変引数をcoへ移動して登録)。
    // fnIdx はfnのスタック位置。生成したタスクを返す(安全上限超過時はluaL_error)。
    static EngineTask* createEngineTask(lua_State* L, int fnIdx, float delaySec);
    // タスクのコルーチンをresumeし、完了/エラーを処理する
    void resumeEngineTask(EngineTask& task, int nargs);
    // delay消化・睡眠再開・完了タスクの掃除(update()から毎フレーム呼ぶ)
    void updateEngineTasks(float deltaTime);
    void pushPathResult(lua_State* state,
                        const std::vector<Pathfinding::PathWaypoint>& waypoints);
    void resumePathScript(PendingPathCoroutine& pending,
                          const std::vector<Pathfinding::PathWaypoint>& waypoints);
    void resumePathSignal(PendingPathCoroutine& pending,
                          const std::vector<Pathfinding::PathWaypoint>& waypoints);

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
    static int signalevent_fire_closure(lua_State* L);

public:
    LuauEngine();
    ~LuauEngine();

    void setBindings(const std::shared_ptr<Instance>& instance);

    void setGlobalInstance(const std::string& name, const std::shared_ptr<Instance>& instance);
    void clearGlobalInstance(const std::string& name);

    bool execute(Script& script);
    // WaitChild で yield 中のスクリプトを、見つかった子（無ければ nullptr）を結果に渡して再開する
    void resumeWaitChild(Script& script, Instance* childOrNull);
    // WaitChild 待機中のスクリプトを毎フレーム評価し、子の出現/タイムアウトで再開する
    void pollWaitChild(Script& script, float deltaTime);

    void setWorkspace(const std::shared_ptr<Workspace>& ws);
    void setSystem(System* s);
    void setRuntimeFileSystem(std::shared_ptr<RuntimeFileSystem> fs) { m_runtimeFileSystem = std::move(fs); }
    RuntimeFileSystem* runtimeFileSystem() const { return m_runtimeFileSystem.get(); }
    System* system() const { return m_system; }

    void executeWorkspaceScripts(Workspace& ws);
    // Workspace外(System配下)に登録されたスクリプトを実行する
    void executeSystemScripts();
    // requireのモジュールキャッシュを破棄する(シーンロードのたびに呼ぶ)
    void clearModuleCache();
    // 未完了のエンジンタスクを全て破棄する(シーンロードのたびに呼ぶ。
    // 旧シーン向けのtask.delayコールバックが次のシーンで発火するのを防ぐ)
    void cancelAllTasks();
    // 非同期ナビメッシュ要求をポーリングし、完了したFindPathコルーチンだけを再開する。
    // ワールド更新を停止しているフレームからも呼べる。
    void pollPathfindingRequests();
    // Signal/taskの通常yieldとFindPath待機を区別するための照会。
    bool isPathfindingCoroutine(lua_State* co) const;
    // System配下(Workspace外)スクリプトの実行状態をリセットする。
    // これらはPlay/Stopで破棄されないため、Play開始時にホストが呼ぶこと。
    void resetSystemScripts();
    void update(float deltaTime);

    // Instance.new/Clone が保持する強参照のうち、ツリー所有済み/破棄済みのものを解放する。
    static void sweepOwnedInstances();

    void fireHeartbeat(float dt);
    void fireNetworkRoleChanged(NetworkRole oldRole, NetworkRole newRole);
    void fireChatMessage(ChatService* service, PeerId senderId, const std::string& text);
    void onCollision(BaseCube* a, BaseCube* b);

    // 1フレームのClone/Restart上限を超えた時にtrueを返す(1回だけ)。
    // ホスト(main.cpp / game_main.cpp)が毎フレーム呼び、消費する。
    bool consumeSafetyHaltRequest();
    // このフレーム分のClone/Restartカウンタをリセットする。ホストがそのフレームの
    // スクリプト/Heartbeat処理を始める直前に必ず1回呼ぶこと。
    void resetFrameSafetyCounters();

    static void pushSignal(lua_State* L, std::shared_ptr<RCBNScriptSignal> sig);
    static void pushConnection(lua_State* L, std::shared_ptr<RCBNScriptConnection> conn);
    static void pushVector2(lua_State* L, Vector2 v);
    // Instance参照をLuauのRCBN_INST_METATABLE userdata(weak_ptr<Instance>)として積む。
    // LuauEngine.cpp外(例: User::spawnCharacter)からシグナル引数を組み立てる際に使う
    static void pushInstance(lua_State* L, const std::shared_ptr<Instance>& inst);

    void onGuiButtonActivated(GuiButton* btn);
};
