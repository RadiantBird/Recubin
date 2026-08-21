#include "include/Core/LuauEngine.hpp"
#include "include/Core/PropertyRegistry.hpp"
#include "include/Core/PhysicalFileInstanceRegistry.hpp"
#include "include/Core/Physics.hpp"
#include "include/Core/RCBNScriptSignal.hpp"
#include "include/Instances/Workspace.hpp"
#include "include/Instances/Decal.hpp"
#include "include/Instances/Motor.hpp"
#include "include/Instances/Sound.hpp"
#include "include/Instances/Lighting.hpp"
#include "include/Instances/Rope.hpp"
#include "include/Instances/Rod.hpp"
#include "include/Instances/BallSocket.hpp"
#include "include/Instances/NoCollision.hpp"
#include "include/Instances/Weld.hpp"
#include "include/Instances/Force.hpp"
#include "include/Instances/Humanoid.hpp"
#include "include/Instances/Animation.hpp"
#include "include/Core/User.hpp"
#include "include/Instances/UserInput.hpp"
#include "include/Instances/AppImage.hpp"
#include "include/Instances/MeshCube.hpp"
#include "include/Instances/Script.hpp"
#include "include/Instances/System.hpp"
#include "include/Network/NetworkManager.hpp"
#include "include/Instances/Event.hpp"
#include "include/Instances/SignalEvent.hpp"
#include "include/Instances/ScreenGuiObject.hpp"
#include "include/Instances/GuiButton.hpp"
#include "include/Instances/TextLabel.hpp"
#include "include/Instances/TextButton.hpp"
#include "include/Instances/WorldGuiObject.hpp"
#include "include/Instances/SurfaceGui.hpp"
#include "include/Instances/BillboardGui.hpp"
#include "include/Instances/ProximityPrompt.hpp"
#include "include/Instances/FileRef.hpp"
#include "include/Instances/Texture.hpp"
#include "include/Instances/ImageLabel.hpp"
#include "include/Instances/ImageButton.hpp"
#include "include/Core/Terrain.hpp"
#include "include/Instances/PathfindingService.hpp"
#include "include/Instances/ChatService.hpp"
#include "include/Instances/NumberValue.hpp"
#include "include/Instances/QuaternionValue.hpp"
#include "include/Instances/CFrameValue.hpp"
#include "include/Instances/ObjectValue.hpp"

// ─── Binding helper factories (anonymous, internal to this TU) ────────────────
//
//  Getter factories  — each returns a lambda matching the DispatchTable signature.
//  Setter factories  — each returns a lambda matching the SetterTable signature.
//
//  Template parameters
//    T   : concrete Instance subclass (e.g. BaseCube, Rope, TextLabel)
//    M   : non-type member-pointer to a *data field*  (e.g. &Rope::LineWidth)
//    Fn  : non-type member-pointer to a *method*      (e.g. &Sound::isPlaying)
//
//  Requires C++17 (auto non-type template parameters, if constexpr).
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// ── Getter: numeric field (float / int / unsigned / enum) ────────────────────
template<typename T, auto M>
auto getter_number() {
    return [](lua_State* L, Instance* obj) -> int {
        auto val = static_cast<T*>(obj)->*M;
        using V = decltype(val);
        if constexpr (std::is_enum_v<V>)
            lua_pushnumber(L, static_cast<lua_Number>(
                static_cast<std::underlying_type_t<V>>(val)));
        else
            lua_pushnumber(L, static_cast<lua_Number>(val));
        return 1;
    };
}

// ── Getter: integer field (uses lua_pushinteger, e.g. ZIndex) ────────────────
template<typename T, auto M>
auto getter_int() {
    return [](lua_State* L, Instance* obj) -> int {
        lua_pushinteger(L, static_cast<lua_Integer>(static_cast<T*>(obj)->*M));
        return 1;
    };
}

// ── Getter: boolean field ────────────────────────────────────────────────────
template<typename T, auto M>
auto getter_bool() {
    return [](lua_State* L, Instance* obj) -> int {
        lua_pushboolean(L, static_cast<T*>(obj)->*M ? 1 : 0);
        return 1;
    };
}

// ── Getter: std::string field ────────────────────────────────────────────────
template<typename T, auto M>
auto getter_string() {
    return [](lua_State* L, Instance* obj) -> int {
        lua_pushstring(L, (static_cast<T*>(obj)->*M).c_str());
        return 1;
    };
}

// ── Getter: Vector3 userdata field ───────────────────────────────────────────
template<typename T, auto M>
auto getter_vec3() {
    return [](lua_State* L, Instance* obj) -> int {
        Vector3* v = (Vector3*)lua_newuserdata(L, sizeof(Vector3));
        *v = static_cast<T*>(obj)->*M;
        luaL_getmetatable(L, LuauEngine::RCBN_VEC3_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    };
}

// ── Getter: Color4 userdata field ────────────────────────────────────────────
template<typename T, auto M>
auto getter_color4() {
    return [](lua_State* L, Instance* obj) -> int {
        Color4* c = (Color4*)lua_newuserdata(L, sizeof(Color4));
        *c = static_cast<T*>(obj)->*M;
        luaL_getmetatable(L, LuauEngine::RCBN_COLOR4_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    };
}

// ── Getter: Vector2 field (via LuauEngine::pushVector2) ──────────────────────
template<typename T, auto M>
auto getter_vec2() {
    return [](lua_State* L, Instance* obj) -> int {
        LuauEngine::pushVector2(L, static_cast<T*>(obj)->*M);
        return 1;
    };
}

// ── Getter: signal field (via LuauEngine::pushSignal) ────────────────────────
template<typename T, auto M>
auto getter_signal() {
    return [](lua_State* L, Instance* obj) -> int {
        LuauEngine::pushSignal(L, static_cast<T*>(obj)->*M);
        return 1;
    };
}

// ── Getter: no-arg getter method → lua number ────────────────────────────────
template<typename T, auto Fn>
auto getter_method_number() {
    return [](lua_State* L, Instance* obj) -> int {
        lua_pushnumber(L, static_cast<lua_Number>((static_cast<T*>(obj)->*Fn)()));
        return 1;
    };
}

// ── Getter: no-arg getter method → lua boolean ───────────────────────────────
template<typename T, auto Fn>
auto getter_method_bool() {
    return [](lua_State* L, Instance* obj) -> int {
        lua_pushboolean(L, (static_cast<T*>(obj)->*Fn)() ? 1 : 0);
        return 1;
    };
}

// ── Getter: push a C closure with the current object as the first upvalue ────
//  Used for methods that are exposed as closures (FindChild, Play, Raycast …).
inline auto getter_closure(lua_CFunction fn, const char* name) {
    return [fn, name](lua_State* L, Instance* obj) -> int {
        auto* ud = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
        new (ud) std::weak_ptr<Instance>(obj->shared_from_this());
        luaL_getmetatable(L, LuauEngine::RCBN_INST_METATABLE);
        lua_setmetatable(L, -2);
        lua_pushcclosure(L, fn, name, 1);
        return 1;
    };
}

// ── Setter: numeric/enum field ────────────────────────────────────────────────
template<typename T, auto M>
auto setter_number() {
    return [](lua_State* L, Instance* obj) -> int {
        using F = std::remove_reference_t<decltype(std::declval<T&>().*M)>;
        if constexpr (std::is_enum_v<F>)
            static_cast<T*>(obj)->*M =
                static_cast<F>(static_cast<int>(luaL_checknumber(L, 3)));
        else
            static_cast<T*>(obj)->*M = static_cast<F>(luaL_checknumber(L, 3));
        return 0;
    };
}

// ── Setter: integer field (lua_checkinteger, e.g. ZIndex) ────────────────────
template<typename T, auto M>
auto setter_int() {
    return [](lua_State* L, Instance* obj) -> int {
        using F = std::remove_reference_t<decltype(std::declval<T&>().*M)>;
        static_cast<T*>(obj)->*M = static_cast<F>(luaL_checkinteger(L, 3));
        return 0;
    };
}

// ── Setter: boolean field ────────────────────────────────────────────────────
template<typename T, auto M>
auto setter_bool() {
    return [](lua_State* L, Instance* obj) -> int {
        static_cast<T*>(obj)->*M = lua_toboolean(L, 3) != 0;
        return 0;
    };
}

// ── Setter: std::string field ────────────────────────────────────────────────
template<typename T, auto M>
auto setter_string() {
    return [](lua_State* L, Instance* obj) -> int {
        static_cast<T*>(obj)->*M = luaL_checkstring(L, 3);
        return 0;
    };
}

// ── Setter: Vector3 userdata field ───────────────────────────────────────────
template<typename T, auto M>
auto setter_vec3() {
    return [](lua_State* L, Instance* obj) -> int {
        Vector3* v = (Vector3*)luaL_checkudata(L, 3, LuauEngine::RCBN_VEC3_METATABLE);
        static_cast<T*>(obj)->*M = *v;
        return 0;
    };
}

// ── Setter: Color4 userdata field ────────────────────────────────────────────
template<typename T, auto M>
auto setter_color4() {
    return [](lua_State* L, Instance* obj) -> int {
        Color4* c = (Color4*)luaL_checkudata(L, 3, LuauEngine::RCBN_COLOR4_METATABLE);
        static_cast<T*>(obj)->*M = *c;
        return 0;
    };
}

// ── Setter: Vector2 userdata field ───────────────────────────────────────────
template<typename T, auto M>
auto setter_vec2() {
    return [](lua_State* L, Instance* obj) -> int {
        Vector2* v = (Vector2*)luaL_checkudata(L, 3, LuauEngine::RCBN_VEC2_METATABLE);
        static_cast<T*>(obj)->*M = *v;
        return 0;
    };
}

// ── Setter: single-float setter method (e.g. setVolume, setMaxDistance) ──────
template<typename T, auto Fn>
auto setter_method_float() {
    return [](lua_State* L, Instance* obj) -> int {
        (static_cast<T*>(obj)->*Fn)(static_cast<float>(luaL_checknumber(L, 3)));
        return 0;
    };
}

// ── Setter: single-bool setter method (e.g. setAnchored, setLooping) ─────────
template<typename T, auto Fn>
auto setter_method_bool() {
    return [](lua_State* L, Instance* obj) -> int {
        (static_cast<T*>(obj)->*Fn)(lua_toboolean(L, 3) != 0);
        return 0;
    };
}

// ── Setter: BaseCube reference setter method (e.g. Weld/Rope/Rod/Motor の Cube0/Cube1) ──
template<typename T, void (T::*Setter)(std::shared_ptr<BaseCube>)>
auto setter_cube_ref() {
    return [](lua_State* L, Instance* obj) -> int {
        auto* ud = (std::weak_ptr<Instance>*)luaL_checkudata(L, 3, LuauEngine::RCBN_INST_METATABLE);
        auto inst = ud->lock();
        if (inst && inst->IsA("BaseCube"))
            (static_cast<T*>(obj)->*Setter)(std::static_pointer_cast<BaseCube>(inst));
        return 0;
    };
}

// ── Setter: setProperty(name, string) 経由 ───────────────────────────────────
//  Motor/Rope/Rod の Attachment0/Attachment1 のように、文字列の設定に加えて
//  weak_ptr のリセットと registerIfReady() の再解決という副作用が必要なプロパティ用。
auto setter_property_string(const char* propName) {
    return [propName](lua_State* L, Instance* obj) -> int {
        YAML::Node n; n = std::string(luaL_checkstring(L, 3));
        obj->setProperty(propName, n);
        return 0;
    };
}

// ── FileRef.Source 用: arg3 が FileRef インスタンスならその Path を out へ取り出す ──
//  生パス文字列ではなく「シーンに存在し YAML/Packager に追跡された FileRef」だけを受ける。
static bool getFileRefPath(lua_State* L, int idx, std::string& out) {
    auto* ud = (std::weak_ptr<Instance>*)luaL_checkudata(L, idx, LuauEngine::RCBN_INST_METATABLE);
    auto inst = ud->lock();
    if (inst && inst->IsA("FileRef")) {
        out = static_cast<FileRef*>(inst.get())->Path;
        return true;
    }
    return false;  // FileRef でなければ何もしない
}

// ── GUI string-conversion helpers (Norm / Face / BillboardMode) ──────────────
static const char* normToStr(Norm n) { return n == Norm::Scale ? "Scale" : "Pixel"; }
static Norm        strToNorm(const char* s) {
    return (s && std::string_view(s) == "Scale") ? Norm::Scale : Norm::Pixel;
}

static const char* faceToStr(Face f) {
    switch (f) {
        case Face::Back:   return "Back";
        case Face::Top:    return "Top";
        case Face::Bottom: return "Bottom";
        case Face::Right:  return "Right";
        case Face::Left:   return "Left";
        default:           return "Front";
    }
}
static Face strToFace(const char* s) {
    if (!s) return Face::Front;
    std::string_view v(s);
    if (v == "Back")   return Face::Back;
    if (v == "Top")    return Face::Top;
    if (v == "Bottom") return Face::Bottom;
    if (v == "Right")  return Face::Right;
    if (v == "Left")   return Face::Left;
    return Face::Front;
}

} // anonymous namespace


// ==================== Getter: Instance, BaseCube ====================
void LuauEngine::InitDispatchTable_Base() {
    // --- Instance ---
    DispatchTable["Instance"]["Name"] = [](lua_State* L, Instance* obj) {
        lua_pushstring(L, obj->Name.c_str());
        return 1;
    };
    DispatchTable["Instance"]["Parent"] = [](lua_State* L, Instance* obj) {
        auto parent = obj->Parent.lock();
        if (!parent) { lua_pushnil(L); return 1; }
        auto* ud = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
        new (ud) std::weak_ptr<Instance>(parent);
        luaL_getmetatable(L, RCBN_INST_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    };
    DispatchTable["Instance"]["FindChild"]   = getter_closure(instance_find_child_closure,   "FindChild");
    DispatchTable["Instance"]["GetChildren"] = getter_closure(instance_get_children_closure, "GetChildren");
    DispatchTable["Instance"]["WaitChild"]   = getter_closure(instance_wait_child_closure,   "WaitChild");
    DispatchTable["Instance"]["IsA"]         = getter_closure(instance_is_a_closure,         "IsA");
    DispatchTable["Instance"]["Destroy"]     = getter_closure(instance_destroy_closure,      "Destroy");
    DispatchTable["Instance"]["Clone"]       = getter_closure(instance_clone_closure,        "Clone");

    // --- Spatial（Position/Size/Rotation/CFrame を基底で公開。Model/Sound にも波及）---
    // Position/Rotation は cframe への参照エイリアスでメンバポインタ不可のため手書き。
    DispatchTable["Spatial"]["Position"] = [](lua_State* L, Instance* obj) {
        pushVector3(L, static_cast<Spatial*>(obj)->cframe.Position);
        return 1;
    };
    DispatchTable["Spatial"]["Size"]     = getter_vec3<Spatial, &Spatial::Size>();
    DispatchTable["Spatial"]["Rotation"] = [](lua_State* L, Instance* obj) {
        pushQuaternion(L, static_cast<Spatial*>(obj)->cframe.Rotation);
        return 1;
    };
    DispatchTable["Spatial"]["CFrame"] = [](lua_State* L, Instance* obj) {
        pushCFrame(L, static_cast<Spatial*>(obj)->cframe);
        return 1;
    };
    DispatchTable["Spatial"]["WorldPosition"] = [](lua_State* L, Instance* obj) {
        pushVector3(L, static_cast<Spatial*>(obj)->getWorldPosition());
        return 1;
    };
    DispatchTable["Spatial"]["WorldCFrame"] = [](lua_State* L, Instance* obj) {
        pushCFrame(L, static_cast<Spatial*>(obj)->getWorldCFrame());
        return 1;
    };

    // --- BaseCube（Position/Size は Spatial に集約。物理特有のみ残置）---
    DispatchTable["BaseCube"]["Color"]        = getter_color4 <BaseCube, &BaseCube::Color>();
    DispatchTable["BaseCube"]["Anchored"]     = getter_bool   <BaseCube, &BaseCube::Anchored>();
    DispatchTable["BaseCube"]["CanCollide"]   = getter_bool   <BaseCube, &BaseCube::CanCollide>();
    DispatchTable["BaseCube"]["CastShadow"]   = getter_bool   <BaseCube, &BaseCube::CastShadow>();
    DispatchTable["BaseCube"]["Unlit"]        = getter_bool   <BaseCube, &BaseCube::Unlit>();
    DispatchTable["BaseCube"]["UseTriplanar"] = getter_bool   <BaseCube, &BaseCube::UseTriplanar>();
    DispatchTable["BaseCube"]["TextureScale"] = getter_number <BaseCube, &BaseCube::TextureScale>();
    DispatchTable["BaseCube"]["Locked"]       = getter_bool   <BaseCube, &BaseCube::Locked>();
    DispatchTable["BaseCube"]["Touched"]      = getter_signal <BaseCube, &BaseCube::Touched>();
    DispatchTable["BaseCube"]["CCDMode"]      = [](lua_State* L, Instance* obj) {
        const auto* cube = static_cast<BaseCube*>(obj);
        lua_pushstring(L, cube->CollisionDetection == CCDMode::Bullet ? "Bullet" : "Default");
        return 1;
    };
    // Velocity is read from the physics actor at runtime, no direct field to bind
    DispatchTable["BaseCube"]["Velocity"]   = [](lua_State* L, Instance* obj) {
        auto* cube = static_cast<BaseCube*>(obj);
        Vector3* v = (Vector3*)lua_newuserdata(L, sizeof(Vector3));
        *v = Vector3(0.0f, 0.0f, 0.0f);
        Physics* physics = cube->lastWorkspace
            ? cube->lastWorkspace->getPhysicsEngine()
            : nullptr;
        if (physics) *v = physics->getLinearVelocity(*cube);
        luaL_getmetatable(L, RCBN_VEC3_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    };

    PropertyRegistry::applyToDispatch("LiquidCube", DispatchTable, SetterTable);
    PropertyRegistry::applyToDispatch("SpawnLocation", DispatchTable, SetterTable);
    PropertyRegistry::applyToDispatch("Force", DispatchTable, SetterTable);
    // Force.Value を既存のスキーマ名として維持しつつ、Luau では一般的な Force 名でも扱えるようにする。
    DispatchTable["Force"]["Force"] = getter_vec3<Force, &Force::Value>();
    SetterTable["Force"]["Force"] = setter_vec3<Force, &Force::Value>();
    PropertyRegistry::applyToDispatch("Sun",  DispatchTable, SetterTable);
    PropertyRegistry::applyToDispatch("Moon", DispatchTable, SetterTable);
}

// ==================== Getter: Workspace, Decal, Lighting, System, Event ====================
void LuauEngine::InitDispatchTable_World() {
    DispatchTable["Workspace"]["Gravity"]        = getter_vec3<Workspace, &Workspace::Gravity>();
    DispatchTable["Workspace"]["Wind"]           = getter_vec3<Workspace, &Workspace::Wind>();
    DispatchTable["Workspace"]["PhysicsEnabled"] = getter_bool<Workspace, &Workspace::PhysicsEnabled>();
    DispatchTable["Workspace"]["Raycast"]        = getter_closure(workspace_raycast_closure, "Raycast");

    DispatchTable["Decal"]["TextureID"]   = getter_number<Decal, &Decal::TextureID>();
    DispatchTable["Decal"]["Face"]        = getter_number<Decal, &Decal::face>();
    DispatchTable["Decal"]["Mode"]        = getter_number<Decal, &Decal::Mode>();
    DispatchTable["Decal"]["Color"]       = getter_color4<Decal, &Decal::Color>();
    // TexturePath は読み取りのみ（書込はテクスチャ再読込が必要で、パス問題と同様に未対応）
    DispatchTable["Decal"]["TexturePath"] = getter_string<Decal, &Decal::texturePath>();

    PropertyRegistry::applyToDispatch("Lighting", DispatchTable, SetterTable);
    PropertyRegistry::applyToDispatch("LightSource", DispatchTable, SetterTable);
    PropertyRegistry::applyToDispatch("PointLight", DispatchTable, SetterTable);
    PropertyRegistry::applyToDispatch("SpotLight", DispatchTable, SetterTable);

    PropertyRegistry::applyToDispatch("ParticleEmitter", DispatchTable, SetterTable);
    DispatchTable["ParticleEmitter"]["Emit"] = getter_closure(particle_emitter_emit_closure, "Emit");

    PropertyRegistry::applyToDispatch("Weather", DispatchTable, SetterTable);

    DispatchTable["System"]["Heartbeat"] = getter_signal<System, &System::Heartbeat>();
    DispatchTable["System"]["NetworkRoleChanged"] = getter_signal<System, &System::NetworkRoleChanged>();
    // 読み取り専用: 現在のネットワークロール("Offline"/"Host"/"Client")とローカルPeerId
    DispatchTable["System"]["NetworkRole"] = [](lua_State* L, Instance*) {
        lua_pushstring(L, NetworkManager::roleToString(NetworkManager::get().getRole()));
        return 1;
    };
    DispatchTable["System"]["LocalPeerId"] = [](lua_State* L, Instance*) {
        lua_pushnumber(L, static_cast<double>(NetworkManager::get().getLocalPeerId()));
        return 1;
    };
    PropertyRegistry::applyToDispatch("System", DispatchTable, SetterTable);

    DispatchTable["ChatService"]["MessageReceived"] = getter_signal<ChatService, &ChatService::MessageReceived>();
    DispatchTable["ChatService"]["SendMessage"] = getter_closure(chat_send_message_closure, "SendMessage");

    DispatchTable["Event"]["Fire"] = getter_closure(LuauEngine::event_fire_closure, "Fire");

    PropertyRegistry::applyToDispatch("SignalEvent", DispatchTable, SetterTable);
    DispatchTable["SignalEvent"]["Fire"] = getter_closure(signalevent_fire_closure, "Fire");
}

// ==================== Getter: Rope, Rod, Weld, Motor ====================
void LuauEngine::InitDispatchTable_Physics() {
    DispatchTable["Rope"]["Attachment0"] = getter_string<Rope, &Rope::m_attachment0Name>();
    DispatchTable["Rope"]["Attachment1"] = getter_string<Rope, &Rope::m_attachment1Name>();
    DispatchTable["Rope"]["MaxDistance"] = getter_number <Rope, &Rope::MaxDistance>();
    DispatchTable["Rope"]["Stiffness"]   = getter_number <Rope, &Rope::Stiffness>();
    DispatchTable["Rope"]["Damping"]     = getter_number <Rope, &Rope::Damping>();
    DispatchTable["Rope"]["LineWidth"]   = getter_number <Rope, &Rope::LineWidth>();
    DispatchTable["Rope"]["Color"]       = getter_color4 <Rope, &Rope::Color>();

    DispatchTable["Rod"]["Attachment0"] = getter_string<Rod, &Rod::m_attachment0Name>();
    DispatchTable["Rod"]["Attachment1"] = getter_string<Rod, &Rod::m_attachment1Name>();
    DispatchTable["Rod"]["LineWidth"] = getter_number <Rod, &Rod::LineWidth>();
    DispatchTable["Rod"]["Color"]     = getter_color4 <Rod, &Rod::Color>();

    DispatchTable["BallSocket"]["Attachment0"] = getter_string<BallSocket, &BallSocket::m_attachment0Name>();
    DispatchTable["BallSocket"]["Attachment1"] = getter_string<BallSocket, &BallSocket::m_attachment1Name>();

    DispatchTable["Weld"]["Cube0"] = getter_string<Weld, &Weld::m_cube0Name>();
    DispatchTable["Weld"]["Cube1"] = getter_string<Weld, &Weld::m_cube1Name>();

    DispatchTable["NoCollision"]["Cube0"] = getter_string<NoCollision, &NoCollision::m_cube0Name>();
    DispatchTable["NoCollision"]["Cube1"] = getter_string<NoCollision, &NoCollision::m_cube1Name>();

    DispatchTable["Motor"]["Attachment0"]   = getter_string<Motor, &Motor::m_attachment0Name>();
    DispatchTable["Motor"]["Attachment1"]   = getter_string<Motor, &Motor::m_attachment1Name>();
    DispatchTable["Motor"]["DriveVelocity"] = getter_number<Motor, &Motor::DriveVelocity>();
    DispatchTable["Motor"]["MaxForce"]      = getter_number<Motor, &Motor::MaxForce>();
    DispatchTable["Motor"]["Axis"]          = getter_vec3  <Motor, &Motor::Axis>();
}

// ==================== Getter: Sound, Humanoid, AppImage, Script ====================
void LuauEngine::InitDispatchTable_Misc() {
    // Sound — properties backed by getter methods
    DispatchTable["Sound"]["IsPlaying"]     = getter_method_bool  <Sound, &Sound::isPlaying>();
    DispatchTable["Sound"]["Looped"]        = getter_method_bool  <Sound, &Sound::isLooping>();
    DispatchTable["Sound"]["Volume"]        = getter_method_number<Sound, &Sound::getVolume>();
    DispatchTable["Sound"]["Speed"]         = getter_method_number<Sound, &Sound::getSpeed>();
    DispatchTable["Sound"]["PreservePitch"] = getter_method_bool  <Sound, &Sound::getPreservePitch>();
    DispatchTable["Sound"]["TimePosition"]  = getter_method_number<Sound, &Sound::getPlaybackTime>();
    DispatchTable["Sound"]["Length"]        = getter_method_number<Sound, &Sound::getLength>();
    DispatchTable["Sound"]["Play"]          = getter_closure(sound_play_closure,  "Play");
    DispatchTable["Sound"]["Stop"]          = getter_closure(sound_stop_closure,  "Stop");
    DispatchTable["Sound"]["Reset"]         = getter_closure(sound_reset_closure, "Reset");
    DispatchTable["Sound"]["Seek"]          = getter_closure(sound_seek_closure,  "Seek");
    DispatchTable["Sound"]["AutoPlay"]      = getter_bool<Sound, &Sound::autoPlay>();
    DispatchTable["Sound"]["ContentPath"]   = [](lua_State* L, Instance* o) {  // read-only
        lua_pushstring(L, static_cast<Sound*>(o)->getContentPath().c_str()); return 1;
    };
    DispatchTable["Sound"]["SoundGroup"]    = [](lua_State* L, Instance* o) {  // read-only
        lua_pushstring(L, static_cast<Sound*>(o)->getSoundGroup().c_str()); return 1;
    };

    // Humanoid — フィールド/シグナルの getter/setter は PropertyRegistry の表から流し込む
    // （WalkSpeed/JumpPower/MaxHealth/RespawnTime/Health/Died）。下記メソッド系のみ手書き。
    PropertyRegistry::applyToDispatch("Humanoid", DispatchTable, SetterTable);
    auto pushAnimationReference = [](lua_State* L, const std::shared_ptr<Animation>& animation) -> int {
        if (animation) LuauEngine::pushInstance(L, animation);
        else lua_pushnil(L);
        return 1;
    };
    DispatchTable["Humanoid"]["WalkAnimation"] = [pushAnimationReference](lua_State* L, Instance* o) {
        return pushAnimationReference(L, static_cast<Humanoid*>(o)->getWalkAnimation());
    };
    DispatchTable["Humanoid"]["JumpAnimation"] = [pushAnimationReference](lua_State* L, Instance* o) {
        return pushAnimationReference(L, static_cast<Humanoid*>(o)->getJumpAnimation());
    };
    DispatchTable["Humanoid"]["EquipAnimation"] = [pushAnimationReference](lua_State* L, Instance* o) {
        return pushAnimationReference(L, static_cast<Humanoid*>(o)->getEquipAnimation());
    };
    auto readAnimationReference = [](lua_State* L) -> std::shared_ptr<Animation> {
        if (lua_isnil(L, 3)) return nullptr;
        auto* ud = (std::weak_ptr<Instance>*)luaL_checkudata(L, 3, LuauEngine::RCBN_INST_METATABLE);
        return std::dynamic_pointer_cast<Animation>(ud->lock());
    };
    SetterTable["Humanoid"]["WalkAnimation"] = [readAnimationReference](lua_State* L, Instance* o) {
        static_cast<Humanoid*>(o)->setWalkAnimation(readAnimationReference(L)); return 0;
    };
    SetterTable["Humanoid"]["JumpAnimation"] = [readAnimationReference](lua_State* L, Instance* o) {
        static_cast<Humanoid*>(o)->setJumpAnimation(readAnimationReference(L)); return 0;
    };
    SetterTable["Humanoid"]["EquipAnimation"] = [readAnimationReference](lua_State* L, Instance* o) {
        static_cast<Humanoid*>(o)->setEquipAnimation(readAnimationReference(L)); return 0;
    };
    DispatchTable["Humanoid"]["PlayAnimation"]  = getter_closure(humanoid_play_animation_closure,  "PlayAnimation");
    DispatchTable["Humanoid"]["PauseAnimation"] = getter_closure(humanoid_pause_animation_closure, "PauseAnimation");
    DispatchTable["Humanoid"]["StopAnimation"]  = getter_closure(humanoid_stop_animation_closure,  "StopAnimation");
    DispatchTable["Humanoid"]["TakeDamage"]  = getter_closure(humanoid_take_damage_closure, "TakeDamage");
    DispatchTable["Humanoid"]["MoveToward"]  = getter_closure(humanoid_move_toward_closure, "MoveToward");
    DispatchTable["Humanoid"]["Jump"]        = getter_closure(humanoid_jump_closure,        "Jump");
    DispatchTable["Humanoid"]["KeyframeReached"] = getter_signal<Humanoid, &Humanoid::KeyframeReached>();

    DispatchTable["Animation"]["Length"] = getter_number<Animation, &Animation::Length>();
    DispatchTable["Animation"]["Speed"] = getter_number<Animation, &Animation::Speed>();
    DispatchTable["Animation"]["Looped"] = getter_bool<Animation, &Animation::Looped>();
    DispatchTable["Animation"]["ContentPath"] = [](lua_State* L, Instance* o) {
        lua_pushstring(L, static_cast<Animation*>(o)->ContentPath.c_str()); return 1;
    };
    DispatchTable["Animation"]["Source"] = [](lua_State* L, Instance* o) {
        lua_pushstring(L, static_cast<Animation*>(o)->getSourceName().c_str()); return 1;
    };
    DispatchTable["Animation"]["LoadStatus"] = [](lua_State* L, Instance* o) {
        lua_pushstring(L, static_cast<Animation*>(o)->getLoadStatusName().c_str()); return 1;
    };
    DispatchTable["Animation"]["UsingBuiltInFallback"] = [](lua_State* L, Instance* o) {
        lua_pushboolean(L, static_cast<Animation*>(o)->isUsingBuiltInFallback()); return 1;
    };
    auto setAnimationProperty = [](const char* property) {
        return [property](lua_State* L, Instance* o) {
            YAML::Node value;
            if (std::string_view(property) == "Looped") value = lua_toboolean(L, 3) != 0;
            else if (std::string_view(property) == "ContentPath") value = std::string(luaL_checkstring(L, 3));
            else value = static_cast<float>(luaL_checknumber(L, 3));
            o->setProperty(property, value);
            return 0;
        };
    };
    SetterTable["Animation"]["Length"] = setAnimationProperty("Length");
    SetterTable["Animation"]["Speed"] = setAnimationProperty("Speed");
    SetterTable["Animation"]["Looped"] = setAnimationProperty("Looped");
    SetterTable["Animation"]["ContentPath"] = setAnimationProperty("ContentPath");

    // Seat — Steer/ThrottleはPropertyRegistry経由でLua読取専用として公開(エンジンが着席中に書き込む)
    PropertyRegistry::applyToDispatch("Seat", DispatchTable, SetterTable);

    // User.Input (UserInputService 相当のインスタンスを返す)
    DispatchTable["User"]["Input"] = [](lua_State* L, Instance* obj) -> int {
        auto* u = static_cast<User*>(obj);
        if (!u->Input) { lua_pushnil(L); return 1; }
        auto* ud = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
        new (ud) std::weak_ptr<Instance>(u->Input);
        luaL_getmetatable(L, RCBN_INST_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    };
    // User.Character (読み書き可能): clone後のキャラクター本体(PlayerCharacter)。spawn前/despawn後はnil。
    // Luauから代入すると Humanoid を自動解決し CharacterAdded を発火する。nil代入で ControlMode::Free に切り替わる
    DispatchTable["User"]["Character"] = [](lua_State* L, Instance* obj) -> int {
        auto* u = static_cast<User*>(obj);
        if (!u->character) { lua_pushnil(L); return 1; }
        auto* ud = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
        new (ud) std::weak_ptr<Instance>(u->character);
        luaL_getmetatable(L, RCBN_INST_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    };
    DispatchTable["Tool"]["Activated"]  = getter_signal<Tool, &Tool::Activated>();
    DispatchTable["Tool"]["Equipped"]   = getter_bool  <Tool, &Tool::Equipped>();  // read-only（装着ロジックは別経路）
    DispatchTable["Tool"]["Hand"]       = getter_number<Tool, &Tool::Hand>();
    DispatchTable["Tool"]["Position"]   = getter_vec3<Tool, &Tool::Position>();
    DispatchTable["Tool"]["Rotation"] = [](lua_State* L, Instance* obj) {
        pushQuaternion(L, static_cast<Tool*>(obj)->Rotation);
        return 1;
    };

    // User.CharacterAdded: 新しいキャラクター(PlayerCharacter)がspawnされるたび発火(初回spawn+全respawn)。
    // Luau側にcharacter(Model)を引数で渡す
    DispatchTable["User"]["CharacterAdded"] = getter_signal<User, &User::CharacterAdded>();
    DispatchTable["User"]["AddTool"]    = getter_closure(user_add_tool_closure,    "AddTool");
    DispatchTable["User"]["RemoveTool"] = getter_closure(user_remove_tool_closure, "RemoveTool");
    DispatchTable["User"]["GetTool"]    = getter_closure(user_get_tool_closure,    "GetTool");
    DispatchTable["User"]["GetTools"]   = getter_closure(user_get_tools_closure,   "GetTools");
    DispatchTable["User"]["GetMouseRay"] = getter_closure(user_get_mouse_ray_closure, "GetMouseRay");

    // User.PeerId (読み取り専用): ネットワークPeerId。0=ローカル/未接続。
    // リモートUser(System.Users配下のUser_<id>)はReplicationManagerが生成時に設定する
    DispatchTable["User"]["PeerId"] = [](lua_State* L, Instance* obj) {
        lua_pushnumber(L, static_cast<double>(static_cast<User*>(obj)->peerId));
        return 1;
    };

    // User.ControlMode ("Free"/"Character"/"Program")
    DispatchTable["User"]["ControlMode"] = [](lua_State* L, Instance* obj) {
        auto* u = static_cast<User*>(obj);
        const char* s = u->controlMode == User::ControlMode::Free      ? "Free"
                       : u->controlMode == User::ControlMode::Program  ? "Program"
                                                                        : "Character";
        lua_pushstring(L, s);
        return 1;
    };
    // User.CameraCFrame — ControlMode::Program 中にLuauからカメラを直接制御する
    DispatchTable["User"]["CameraCFrame"] = [](lua_State* L, Instance* obj) {
        pushCFrame(L, static_cast<User*>(obj)->getCameraCFrame());
        return 1;
    };
    DispatchTable["UserInput"]["Pressed"]   = getter_signal<UserInput, &UserInput::Pressed>();
    DispatchTable["UserInput"]["Released"]  = getter_signal<UserInput, &UserInput::Released>();
    DispatchTable["UserInput"]["IsPressed"] = getter_closure(userinput_ispressed_closure, "IsPressed");

    PropertyRegistry::applyToDispatch("AppImage", DispatchTable, SetterTable);
    PropertyRegistry::applyToDispatch("PhysicalFileInstance", DispatchTable, SetterTable);
    for (const auto& type : PhysicalFileInstanceRegistry::types())
        PropertyRegistry::applyToDispatch(type.className, DispatchTable, SetterTable);

    DispatchTable["MeshCube"]["MeshFile"] = getter_string<MeshCube, &MeshCube::MeshFile>();

    DispatchTable["Script"]["Enabled"] = getter_bool  <Script, &Script::Enabled>();
    DispatchTable["Script"]["Path"]    = getter_string<Script, &Script::Path>();
    DispatchTable["Script"]["Source"]  = getter_string<Script, &Script::Source>();
    DispatchTable["Script"]["Aborted"] = getter_bool  <Script, &Script::Aborted>();  // read-only（安全対策のタイムアウト等で自動的にセットされる）
    DispatchTable["Script"]["Restart"] = getter_closure(script_restart_closure, "Restart");

    // ── Value系インスタンス ──
    PropertyRegistry::applyToDispatch("ValueBase",     DispatchTable, SetterTable);
    PropertyRegistry::applyToDispatch("IntValue",      DispatchTable, SetterTable);
    PropertyRegistry::applyToDispatch("BoolValue",     DispatchTable, SetterTable);
    PropertyRegistry::applyToDispatch("Vector3Value",  DispatchTable, SetterTable);
    PropertyRegistry::applyToDispatch("Color4Value",   DispatchTable, SetterTable);

    DispatchTable["NumberValue"]["Value"] = getter_number<NumberValue, &NumberValue::Value>();

    DispatchTable["QuaternionValue"]["Value"] = [](lua_State* L, Instance* obj) -> int {
        pushQuaternion(L, static_cast<QuaternionValue*>(obj)->Value);
        return 1;
    };

    DispatchTable["CFrameValue"]["Value"] = [](lua_State* L, Instance* obj) -> int {
        pushCFrame(L, static_cast<CFrameValue*>(obj)->Value);
        return 1;
    };

    DispatchTable["ObjectValue"]["Value"] = [](lua_State* L, Instance* obj) -> int {
        auto target = static_cast<ObjectValue*>(obj)->getTarget();
        if (target) pushInstance(L, target);
        else        lua_pushnil(L);
        return 1;
    };
}


// ==================== Setter: Instance, BaseCube ====================
void LuauEngine::InitSetterTable_Base() {
    // Name: renameTo() が親の children マップとの整合・衝突時の自動リネームを行う
    SetterTable["Instance"]["Name"] = [](lua_State* L, Instance* obj) {
        obj->renameTo(luaL_checkstring(L, 3));
        return 0;
    };

    // Parent: reparent（nil で親なし化）。setParent が children マップを整合させる。
    SetterTable["Instance"]["Parent"] = [](lua_State* L, Instance* obj) {
        if (lua_isnil(L, 3)) { obj->setParent(nullptr); return 0; }
        auto* ud = (std::weak_ptr<Instance>*)luaL_checkudata(L, 3, RCBN_INST_METATABLE);
        if (auto newParent = ud->lock()) obj->setParent(newParent);
        return 0;
    };

    // --- Spatial: Position/Size/Rotation/CFrame を基底で公開。
    // BaseCube なら物理同期メソッド（teleportTo/setSize/setRotation。親チェーン合成込み）に委譲し、
    // 非 Cube（Model/Sound）は cframe を直接更新する。
    SetterTable["Spatial"]["Position"] = [](lua_State* L, Instance* obj) {
        Vector3* v = (Vector3*)luaL_checkudata(L, 3, RCBN_VEC3_METATABLE);
        if (auto* cube = dynamic_cast<BaseCube*>(obj)) cube->teleportTo(*v);
        else static_cast<Spatial*>(obj)->cframe.Position = *v;
        return 0;
    };
    SetterTable["Spatial"]["Size"] = [](lua_State* L, Instance* obj) {
        Vector3* v = (Vector3*)luaL_checkudata(L, 3, RCBN_VEC3_METATABLE);
        if (auto* cube = dynamic_cast<BaseCube*>(obj)) cube->setSize(*v);
        else static_cast<Spatial*>(obj)->Size = *v;
        return 0;
    };
    SetterTable["Spatial"]["Rotation"] = [](lua_State* L, Instance* obj) {
        Quaternion* q = (Quaternion*)luaL_checkudata(L, 3, LuauEngine::RCBN_QUATERNION_METATABLE);
        if (auto* cube = dynamic_cast<BaseCube*>(obj)) cube->setRotation(*q);
        else static_cast<Spatial*>(obj)->cframe.Rotation = *q;
        return 0;
    };
    SetterTable["Spatial"]["CFrame"] = [](lua_State* L, Instance* obj) {
        CFrame* cf = (CFrame*)luaL_checkudata(L, 3, LuauEngine::RCBN_CFRAME_METATABLE);
        if (auto* cube = dynamic_cast<BaseCube*>(obj)) { cube->teleportTo(cf->Position); cube->setRotation(cf->Rotation); }
        else static_cast<Spatial*>(obj)->cframe = *cf;
        return 0;
    };
    SetterTable["BaseCube"]["Color"]        = setter_color4     <BaseCube, &BaseCube::Color>();
    SetterTable["BaseCube"]["Anchored"]     = setter_method_bool<BaseCube, &BaseCube::setAnchored>();
    SetterTable["BaseCube"]["CanCollide"]   = setter_method_bool<BaseCube, &BaseCube::setCanCollide>();
    SetterTable["BaseCube"]["CastShadow"]   = setter_bool       <BaseCube, &BaseCube::CastShadow>();
    SetterTable["BaseCube"]["Unlit"]        = setter_bool       <BaseCube, &BaseCube::Unlit>();
    SetterTable["BaseCube"]["UseTriplanar"] = setter_bool       <BaseCube, &BaseCube::UseTriplanar>();
    SetterTable["BaseCube"]["TextureScale"] = setter_number     <BaseCube, &BaseCube::TextureScale>();
    SetterTable["BaseCube"]["Locked"]       = setter_method_bool<BaseCube, &BaseCube::setLocked>();
    SetterTable["BaseCube"]["CCDMode"] = [](lua_State* L, Instance* obj) {
        const std::string mode = luaL_checkstring(L, 3);
        static_cast<BaseCube*>(obj)->setCCDMode(
            mode == "Bullet" ? CCDMode::Bullet : CCDMode::Default);
        return 0;
    };
}

// ==================== Setter: Workspace, Decal, Lighting ====================
void LuauEngine::InitSetterTable_World() {
    // Gravity: must also propagate to the physics engine
    SetterTable["Workspace"]["Wind"] = setter_vec3<Workspace, &Workspace::Wind>();

    SetterTable["Workspace"]["Gravity"] = [](lua_State* L, Instance* obj) {
        auto* ws = static_cast<Workspace*>(obj);
        Vector3* v = (Vector3*)luaL_checkudata(L, 3, RCBN_VEC3_METATABLE);
        ws->Gravity = *v;
        if (ws->getPhysicsEngine()) ws->getPhysicsEngine()->setGravity(*v);
        return 0;
    };
    SetterTable["Workspace"]["PhysicsEnabled"] = setter_bool<Workspace, &Workspace::PhysicsEnabled>();

    SetterTable["Decal"]["TextureID"] = setter_number<Decal, &Decal::TextureID>();
    SetterTable["Decal"]["Face"]      = setter_number<Decal, &Decal::face>();
    SetterTable["Decal"]["Mode"]      = setter_number<Decal, &Decal::Mode>();
    SetterTable["Decal"]["Color"]     = setter_color4<Decal, &Decal::Color>();
    // FileRef.Source: 画像 FileRef を代入してテクスチャを適用
    SetterTable["Decal"]["Source"] = [](lua_State* L, Instance* o) {
        std::string p; if (getFileRefPath(L, 3, p)) static_cast<Decal*>(o)->setTexturePath(p);
        return 0;
    };
    SetterTable["Texture"]["Source"] = [](lua_State* L, Instance* o) {
        std::string p; if (getFileRefPath(L, 3, p)) static_cast<Texture*>(o)->setTexturePath(p);
        return 0;
    };

    // Lighting の setter は applyToDispatch（InitDispatchTable_World）で登録済み
}

// ==================== Setter: Weld, Rope, Rod, Motor ====================
void LuauEngine::InitSetterTable_Physics() {
    SetterTable["Weld"]["Cube0"] = setter_cube_ref<Weld, &Weld::setCube0>();
    SetterTable["Weld"]["Cube1"] = setter_cube_ref<Weld, &Weld::setCube1>();

    SetterTable["NoCollision"]["Cube0"] = setter_cube_ref<NoCollision, &NoCollision::setCube0>();
    SetterTable["NoCollision"]["Cube1"] = setter_cube_ref<NoCollision, &NoCollision::setCube1>();

    SetterTable["Rope"]["Cube0"]       = setter_cube_ref     <Rope, &Rope::setCube0>();
    SetterTable["Rope"]["Cube1"]       = setter_cube_ref     <Rope, &Rope::setCube1>();
    SetterTable["Rope"]["Attachment0"] = setter_property_string("Attachment0");
    SetterTable["Rope"]["Attachment1"] = setter_property_string("Attachment1");
    SetterTable["Rope"]["MaxDistance"] = setter_method_float<Rope, &Rope::setMaxDistance>();
    SetterTable["Rope"]["Stiffness"]   = setter_method_float<Rope, &Rope::setStiffness>();
    SetterTable["Rope"]["Damping"]     = setter_method_float<Rope, &Rope::setDamping>();
    SetterTable["Rope"]["LineWidth"]   = setter_number      <Rope, &Rope::LineWidth>();
    SetterTable["Rope"]["Color"]       = setter_color4      <Rope, &Rope::Color>();

    SetterTable["Rod"]["Cube0"]     = setter_cube_ref<Rod, &Rod::setCube0>();
    SetterTable["Rod"]["Cube1"]     = setter_cube_ref<Rod, &Rod::setCube1>();
    SetterTable["Rod"]["Attachment0"] = setter_property_string("Attachment0");
    SetterTable["Rod"]["Attachment1"] = setter_property_string("Attachment1");
    SetterTable["Rod"]["LineWidth"] = setter_number<Rod, &Rod::LineWidth>();
    SetterTable["Rod"]["Color"]     = setter_color4<Rod, &Rod::Color>();

    SetterTable["BallSocket"]["Cube0"]     = setter_cube_ref<BallSocket, &BallSocket::setCube0>();
    SetterTable["BallSocket"]["Cube1"]     = setter_cube_ref<BallSocket, &BallSocket::setCube1>();
    SetterTable["BallSocket"]["Attachment0"] = setter_property_string("Attachment0");
    SetterTable["BallSocket"]["Attachment1"] = setter_property_string("Attachment1");

    SetterTable["Motor"]["Cube0"]         = setter_cube_ref<Motor, &Motor::setCube0>();
    SetterTable["Motor"]["Cube1"]         = setter_cube_ref<Motor, &Motor::setCube1>();
    SetterTable["Motor"]["Attachment0"]   = setter_property_string("Attachment0");
    SetterTable["Motor"]["Attachment1"]   = setter_property_string("Attachment1");
    SetterTable["Motor"]["DriveVelocity"] = setter_method_float<Motor, &Motor::setDriveVelocity>();
    SetterTable["Motor"]["MaxForce"]      = setter_method_float<Motor, &Motor::setMaxForce>();
    SetterTable["Motor"]["Axis"] = [](lua_State* L, Instance* obj) {
        Vector3* axis = (Vector3*)luaL_checkudata(L, 3, RCBN_VEC3_METATABLE);
        static_cast<Motor*>(obj)->setAxis(*axis);
        return 0;
    };
}

// ==================== Setter: Sound, Humanoid, AppImage, Script ====================
void LuauEngine::InitSetterTable_Misc() {
    SetterTable["Sound"]["Looped"]        = setter_method_bool <Sound, &Sound::setLooping>();
    SetterTable["Sound"]["Volume"]        = setter_method_float<Sound, &Sound::setVolume>();
    SetterTable["Sound"]["Speed"]         = setter_method_float<Sound, &Sound::setSpeed>();
    SetterTable["Sound"]["PreservePitch"] = setter_method_bool <Sound, &Sound::setPreservePitch>();
    SetterTable["Sound"]["TimePosition"]  = setter_method_float<Sound, &Sound::seekSeconds>();
    SetterTable["Sound"]["AutoPlay"]      = setter_bool        <Sound, &Sound::autoPlay>();

    SetterTable["Tool"]["Hand"]     = setter_number<Tool, &Tool::Hand>();
    SetterTable["Tool"]["Position"] = setter_vec3<Tool, &Tool::Position>();
    SetterTable["Tool"]["Rotation"] = [](lua_State* L, Instance* obj) {
        Quaternion* q = (Quaternion*)luaL_checkudata(L, 3, LuauEngine::RCBN_QUATERNION_METATABLE);
        static_cast<Tool*>(obj)->Rotation = *q;
        return 0;
    };

    // FileRef.Source: FileRef インスタンスを代入して消費者にロードさせる（生パスは扱わない）
    SetterTable["Sound"]["Source"] = [](lua_State* L, Instance* o) {
        std::string p; if (getFileRefPath(L, 3, p)) static_cast<Sound*>(o)->loadFromFile(p);
        return 0;
    };
    SetterTable["MeshCube"]["Source"] = [](lua_State* L, Instance* o) {
        std::string p; if (getFileRefPath(L, 3, p)) static_cast<MeshCube*>(o)->loadFromGLB(p);
        return 0;
    };
    SetterTable["ImageLabel"]["Source"] = [](lua_State* L, Instance* o) {
        std::string p; if (getFileRefPath(L, 3, p)) static_cast<ImageLabel*>(o)->m_image.setImage(p);
        return 0;
    };
    SetterTable["ImageButton"]["Source"] = [](lua_State* L, Instance* o) {
        std::string p; if (getFileRefPath(L, 3, p)) static_cast<ImageButton*>(o)->m_image.setImage(p);
        return 0;
    };

    // Humanoid のフィールド setter は PropertyRegistry::applyToDispatch（InitDispatchTable_Misc）で登録済み

    // AppImage の setter は applyToDispatch（InitDispatchTable_Misc）で登録済み

    SetterTable["Script"]["Enabled"] = setter_bool  <Script, &Script::Enabled>();
    SetterTable["Script"]["Path"]    = setter_string<Script, &Script::Path>();

    // User.ControlMode ("Free"/"Character"/"Program")
    SetterTable["User"]["ControlMode"] = [](lua_State* L, Instance* obj) {
        std::string s = luaL_checkstring(L, 3);
        auto* u = static_cast<User*>(obj);
        if (s == "Free")         u->controlMode = User::ControlMode::Free;
        else if (s == "Program") u->controlMode = User::ControlMode::Program;
        else                     u->controlMode = User::ControlMode::Character;
        return 0;
    };
    // User.CameraCFrame — ControlMode::Program 中にLuauからカメラを直接制御する
    SetterTable["User"]["CameraCFrame"] = [](lua_State* L, Instance* obj) {
        CFrame* cf = (CFrame*)luaL_checkudata(L, 3, LuauEngine::RCBN_CFRAME_METATABLE);
        static_cast<User*>(obj)->setCameraCFrame(*cf);
        return 0;
    };
    // User.Character セッター: setCharacterFromScript() 参照(Humanoid自動解決/CharacterAdded発火/nil→Free切替はUser側に集約)
    SetterTable["User"]["Character"] = [](lua_State* L, Instance* obj) {
        auto* u = static_cast<User*>(obj);
        if (lua_isnil(L, 3)) {
            u->setCharacterFromScript(nullptr);
        } else {
            auto* ud = (std::weak_ptr<Instance>*)luaL_checkudata(L, 3, RCBN_INST_METATABLE);
            auto model = std::dynamic_pointer_cast<Model>(ud->lock());
            if (!model) {
                luaL_error(L, "User.Character には Model インスタンスのみ代入できます");
                return 0;
            }
            u->setCharacterFromScript(model);
        }
        return 0;
    };

    // ── Value系インスタンス ──
    SetterTable["NumberValue"]["Value"] = [](lua_State* L, Instance* obj) -> int {
        auto* self = static_cast<NumberValue*>(obj);
        self->Value = static_cast<double>(luaL_checknumber(L, 3));
        if (self->Changed) self->Changed->fire([self](lua_State* L2) {
            lua_pushnumber(L2, static_cast<lua_Number>(self->Value));
            return 1;
        });
        return 0;
    };

    SetterTable["QuaternionValue"]["Value"] = [](lua_State* L, Instance* obj) -> int {
        Quaternion* q = (Quaternion*)luaL_checkudata(L, 3, LuauEngine::RCBN_QUATERNION_METATABLE);
        auto* self = static_cast<QuaternionValue*>(obj);
        self->Value = *q;
        if (self->Changed) self->Changed->fire([self](lua_State* L2) {
            pushQuaternion(L2, self->Value);
            return 1;
        });
        return 0;
    };

    SetterTable["CFrameValue"]["Value"] = [](lua_State* L, Instance* obj) -> int {
        CFrame* cf = (CFrame*)luaL_checkudata(L, 3, LuauEngine::RCBN_CFRAME_METATABLE);
        auto* self = static_cast<CFrameValue*>(obj);
        self->Value = *cf;
        if (self->Changed) self->Changed->fire([self](lua_State* L2) {
            pushCFrame(L2, self->Value);
            return 1;
        });
        return 0;
    };

    SetterTable["ObjectValue"]["Value"] = [](lua_State* L, Instance* obj) -> int {
        auto* self = static_cast<ObjectValue*>(obj);
        if (lua_isnil(L, 3)) {
            self->setTarget(nullptr);
        } else {
            auto* ud = (std::weak_ptr<Instance>*)luaL_checkudata(L, 3, RCBN_INST_METATABLE);
            self->setTarget(ud->lock());
        }
        return 0;
    };
}


// ==================== Getter: GUI ====================
void LuauEngine::InitDispatchTable_GUI() {
    // GUI 一族はスキーマ表から get/set を流し込む（Norm/Face/Mode は enum 文字列）
    PropertyRegistry::applyToDispatch("GuiObject",       DispatchTable, SetterTable);
    PropertyRegistry::applyToDispatch("ScreenGuiObject", DispatchTable, SetterTable);
    PropertyRegistry::applyToDispatch("GuiButton",       DispatchTable, SetterTable);
    PropertyRegistry::applyToDispatch("TextLabel",       DispatchTable, SetterTable);
    PropertyRegistry::applyToDispatch("TextButton",      DispatchTable, SetterTable);
    PropertyRegistry::applyToDispatch("ImageLabel",      DispatchTable, SetterTable);
    PropertyRegistry::applyToDispatch("ImageButton",     DispatchTable, SetterTable);
    PropertyRegistry::applyToDispatch("WorldGuiObject",  DispatchTable, SetterTable);
    PropertyRegistry::applyToDispatch("SurfaceGui",      DispatchTable, SetterTable);
    PropertyRegistry::applyToDispatch("BillboardGui",    DispatchTable, SetterTable);
    PropertyRegistry::applyToDispatch("ProximityPrompt", DispatchTable, SetterTable);

    // --- Canvas ---
    PropertyRegistry::applyToDispatch("Canvas", DispatchTable, SetterTable);
    DispatchTable["Canvas"]["SetPixel"]  = getter_closure(canvas_set_pixel_closure,   "SetPixel");
    DispatchTable["Canvas"]["GetPixel"]  = getter_closure(canvas_get_pixel_closure,   "GetPixel");
    DispatchTable["Canvas"]["Clear"]     = getter_closure(canvas_clear_closure,       "Clear");
    DispatchTable["Canvas"]["WorldToUV"] = getter_closure(canvas_world_to_uv_closure, "WorldToUV");

    // --- Terrain ---
    DispatchTable["Terrain"]["Enabled"]    = getter_bool<Terrain, &Terrain::Enabled>();
    DispatchTable["Terrain"]["DataPath"]   = getter_string<Terrain, &Terrain::DataPath>();
    DispatchTable["Terrain"]["Seed"]       = getter_number<Terrain, &Terrain::Seed>();
    DispatchTable["Terrain"]["Flat"]       = getter_bool<Terrain, &Terrain::Flat>();
    DispatchTable["Terrain"]["SetBlock"]    = getter_closure(terrain_set_block_closure,    "SetBlock");
    DispatchTable["Terrain"]["RemoveBlock"] = getter_closure(terrain_remove_block_closure, "RemoveBlock");
    DispatchTable["Terrain"]["GetBlock"]    = getter_closure(terrain_get_block_closure,    "GetBlock");
    DispatchTable["Terrain"]["Raycast"]     = getter_closure(terrain_raycast_closure,      "Raycast");
    DispatchTable["Terrain"]["ApplyBrush"]  = getter_closure(terrain_apply_brush_closure,  "ApplyBrush");

    // --- PathfindingService ---
    // AgentRadius/AgentHeight/AgentMaxClimb/AgentMaxSlope/MaxJumpDistance/MaxJumpHeight は
    // PropertyRegistry の表から流し込む。FindPath のみ手書き。
    PropertyRegistry::applyToDispatch("PathfindingService", DispatchTable, SetterTable);
    DispatchTable["PathfindingService"]["FindPath"] = getter_closure(pathfinding_find_path_closure, "FindPath");
    DispatchTable["PathfindingService"]["Configure"] = getter_closure(pathfinding_configure_closure, "Configure");
}

// ==================== Setter: GUI ====================
void LuauEngine::InitSetterTable_GUI() {
    // GUI 一族の setter は applyToDispatch（InitDispatchTable_GUI）で登録済み

    // --- Terrain ---
    SetterTable["Terrain"]["Enabled"]  = setter_bool<Terrain, &Terrain::Enabled>();
    // DataPath は Lua から書込不可（任意ディレクトリへの地形YAML書出しを防ぐ）。読取は getter で可能。
    SetterTable["Terrain"]["Seed"]     = setter_number<Terrain, &Terrain::Seed>();
    SetterTable["Terrain"]["Flat"]     = setter_bool<Terrain, &Terrain::Flat>();
}
