#include "include/Core/LuauEngine.hpp"
#include "include/Core/Physics.hpp"
#include "include/Core/RCBNScriptSignal.hpp"
#include "include/Instances/Workspace.hpp"
#include "include/Instances/Decal.hpp"
#include "include/Instances/Motor.hpp"
#include "include/Instances/Sound.hpp"
#include "include/Instances/Lighting.hpp"
#include "include/Instances/Rope.hpp"
#include "include/Instances/Rod.hpp"
#include "include/Instances/Weld.hpp"
#include "include/Instances/CharacterSetting.hpp"
#include "include/Instances/AppImage.hpp"
#include "include/Instances/Script.hpp"
#include "include/Instances/System.hpp"
#include "include/Instances/Event.hpp"
#include "include/Instances/ScreenGuiObject.hpp"
#include "include/Instances/GuiButton.hpp"
#include "include/Instances/TextLabel.hpp"
#include "include/Instances/TextButton.hpp"
#include "include/Instances/WorldGuiObject.hpp"
#include "include/Instances/SurfaceGui.hpp"
#include "include/Instances/BillboardGui.hpp"
#include "include/Instances/ProximityPrompt.hpp"

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
        std::cout << "Accessing Name of Instance: " << obj->Name << std::endl;
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
    DispatchTable["Instance"]["IsA"]         = getter_closure(instance_is_a_closure,         "IsA");
    DispatchTable["Instance"]["Destroy"]     = getter_closure(instance_destroy_closure,      "Destroy");

    // --- BaseCube ---
    DispatchTable["BaseCube"]["Position"] = [](lua_State* L, Instance* obj) {
        Vector3* v = (Vector3*)lua_newuserdata(L, sizeof(Vector3));
        *v = static_cast<BaseCube*>(obj)->Position;
        luaL_getmetatable(L, LuauEngine::RCBN_VEC3_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    };
    DispatchTable["BaseCube"]["Size"]       = getter_vec3  <BaseCube, &BaseCube::Size>();
    DispatchTable["BaseCube"]["Color"]      = getter_color4<BaseCube, &BaseCube::Color>();
    DispatchTable["BaseCube"]["Anchored"]   = getter_bool  <BaseCube, &BaseCube::Anchored>();
    DispatchTable["BaseCube"]["CanCollide"] = getter_bool  <BaseCube, &BaseCube::CanCollide>();
    DispatchTable["BaseCube"]["Touched"]    = getter_signal <BaseCube, &BaseCube::Touched>();
    // Velocity is read from the physics actor at runtime, no direct field to bind
    DispatchTable["BaseCube"]["Velocity"]   = [](lua_State* L, Instance* obj) {
        auto* cube = static_cast<BaseCube*>(obj);
        Vector3* v = (Vector3*)lua_newuserdata(L, sizeof(Vector3));
        *v = Vector3(0.0f, 0.0f, 0.0f);
        if (cube->actor) {
            if (auto* dyn = cube->actor->is<physx::PxRigidDynamic>()) {
                physx::PxVec3 vel = dyn->getLinearVelocity();
                *v = Vector3(vel.x, vel.y, vel.z);
            }
        }
        luaL_getmetatable(L, RCBN_VEC3_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    };
}

// ==================== Getter: Workspace, Decal, Lighting, System, Event ====================
void LuauEngine::InitDispatchTable_World() {
    DispatchTable["Workspace"]["Gravity"]        = getter_vec3<Workspace, &Workspace::Gravity>();
    DispatchTable["Workspace"]["PhysicsEnabled"] = getter_bool<Workspace, &Workspace::PhysicsEnabled>();
    DispatchTable["Workspace"]["Raycast"]        = getter_closure(workspace_raycast_closure, "Raycast");

    DispatchTable["Decal"]["TextureID"] = getter_number<Decal, &Decal::TextureID>();
    DispatchTable["Decal"]["Face"]      = getter_number<Decal, &Decal::face>();

    DispatchTable["Lighting"]["Brightness"] = getter_number<Lighting, &Lighting::brightness>();

    DispatchTable["System"]["Heartbeat"] = getter_signal<System, &System::Heartbeat>();

    DispatchTable["Event"]["Fire"] = getter_closure(LuauEngine::event_fire_closure, "Fire");
}

// ==================== Getter: Rope, Rod, Weld, Motor ====================
void LuauEngine::InitDispatchTable_Physics() {
    DispatchTable["Rope"]["MaxDistance"] = getter_number <Rope, &Rope::MaxDistance>();
    DispatchTable["Rope"]["Stiffness"]   = getter_number <Rope, &Rope::Stiffness>();
    DispatchTable["Rope"]["Damping"]     = getter_number <Rope, &Rope::Damping>();
    DispatchTable["Rope"]["LineWidth"]   = getter_number <Rope, &Rope::LineWidth>();
    DispatchTable["Rope"]["Color"]       = getter_color4 <Rope, &Rope::Color>();

    DispatchTable["Rod"]["LineWidth"] = getter_number <Rod, &Rod::LineWidth>();
    DispatchTable["Rod"]["Color"]     = getter_color4 <Rod, &Rod::Color>();

    DispatchTable["Weld"]["Cube0"] = getter_string<Weld, &Weld::m_cube0Name>();
    DispatchTable["Weld"]["Cube1"] = getter_string<Weld, &Weld::m_cube1Name>();

    DispatchTable["Motor"]["DriveVelocity"] = getter_number<Motor, &Motor::DriveVelocity>();
    DispatchTable["Motor"]["MaxForce"]      = getter_number<Motor, &Motor::MaxForce>();
}

// ==================== Getter: Sound, CharacterSetting, AppImage, Script ====================
void LuauEngine::InitDispatchTable_Misc() {
    // Sound — properties backed by getter methods
    DispatchTable["Sound"]["IsPlaying"] = getter_method_bool  <Sound, &Sound::isPlaying>();
    DispatchTable["Sound"]["Looped"]    = getter_method_bool  <Sound, &Sound::isLooping>();
    DispatchTable["Sound"]["Volume"]    = getter_method_number<Sound, &Sound::getVolume>();
    DispatchTable["Sound"]["Play"]      = getter_closure(sound_play_closure, "Play");
    DispatchTable["Sound"]["Stop"]      = getter_closure(sound_stop_closure, "Stop");

    // CharacterSetting
    DispatchTable["CharacterSetting"]["JumpPower"]    = getter_number <CharacterSetting, &CharacterSetting::jumpPower>();
    DispatchTable["CharacterSetting"]["MoveSpeed"]    = getter_number <CharacterSetting, &CharacterSetting::moveSpeed>();
    DispatchTable["CharacterSetting"]["FacePath"]     = getter_string <CharacterSetting, &CharacterSetting::facePath>();
    DispatchTable["CharacterSetting"]["HeadColor"]    = getter_color4 <CharacterSetting, &CharacterSetting::headColor>();
    DispatchTable["CharacterSetting"]["TorsoColor"]   = getter_color4 <CharacterSetting, &CharacterSetting::torsoColor>();
    DispatchTable["CharacterSetting"]["LeftArmColor"]  = getter_color4<CharacterSetting, &CharacterSetting::leftArmColor>();
    DispatchTable["CharacterSetting"]["RightArmColor"] = getter_color4<CharacterSetting, &CharacterSetting::rightArmColor>();
    DispatchTable["CharacterSetting"]["LeftLegColor"]  = getter_color4<CharacterSetting, &CharacterSetting::leftLegColor>();
    DispatchTable["CharacterSetting"]["RightLegColor"] = getter_color4<CharacterSetting, &CharacterSetting::rightLegColor>();

    DispatchTable["AppImage"]["IconPath"] = getter_string<AppImage, &AppImage::iconPath>();

    DispatchTable["Script"]["Enabled"] = getter_bool  <Script, &Script::Enabled>();
    DispatchTable["Script"]["Path"]    = getter_string<Script, &Script::Path>();
    DispatchTable["Script"]["Source"]  = getter_string<Script, &Script::Source>();
}


// ==================== Setter: Instance, BaseCube ====================
void LuauEngine::InitSetterTable_Base() {
    // Name: must keep parent's children map consistent
    SetterTable["Instance"]["Name"] = [](lua_State* L, Instance* obj) {
        std::string newName = luaL_checkstring(L, 3);
        if (obj->Name == newName) return 0;
        auto parent = obj->Parent.lock();
        if (parent) {
            parent->children.erase(obj->Name);
            obj->Name = newName;
            parent->children[newName] = obj->shared_from_this();
        } else {
            obj->Name = newName;
        }
        return 0;
    };

    // Position: must also sync the PhysX actor pose
    SetterTable["BaseCube"]["Position"] = [](lua_State* L, Instance* obj) {
        auto* cube   = static_cast<BaseCube*>(obj);
        Vector3* pos = (Vector3*)luaL_checkudata(L, 3, LuauEngine::RCBN_VEC3_METATABLE);
        cube->Position = *pos;
        if (cube->actor) {
            physx::PxTransform pose = cube->actor->getGlobalPose();
            pose.p = physx::PxVec3(pos->x, pos->y, pos->z);
            cube->actor->setGlobalPose(pose);
        }
        return 0;
    };
    // Size delegates to setSize() to keep physics geometry in sync
    SetterTable["BaseCube"]["Size"] = [](lua_State* L, Instance* obj) {
        Vector3* v = (Vector3*)luaL_checkudata(L, 3, RCBN_VEC3_METATABLE);
        static_cast<BaseCube*>(obj)->setSize(*v);
        return 0;
    };
    SetterTable["BaseCube"]["Color"]      = setter_color4     <BaseCube, &BaseCube::Color>();
    SetterTable["BaseCube"]["Anchored"]   = setter_method_bool<BaseCube, &BaseCube::setAnchored>();
    SetterTable["BaseCube"]["CanCollide"] = setter_bool       <BaseCube, &BaseCube::CanCollide>();
}

// ==================== Setter: Workspace, Decal, Lighting ====================
void LuauEngine::InitSetterTable_World() {
    // Gravity: must also propagate to the physics engine
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

    SetterTable["Lighting"]["Brightness"] = setter_number<Lighting, &Lighting::brightness>();
}

// ==================== Setter: Rope, Rod, Motor ====================
void LuauEngine::InitSetterTable_Physics() {
    SetterTable["Rope"]["MaxDistance"] = setter_method_float<Rope, &Rope::setMaxDistance>();
    SetterTable["Rope"]["Stiffness"]   = setter_method_float<Rope, &Rope::setStiffness>();
    SetterTable["Rope"]["Damping"]     = setter_method_float<Rope, &Rope::setDamping>();
    SetterTable["Rope"]["LineWidth"]   = setter_number      <Rope, &Rope::LineWidth>();
    SetterTable["Rope"]["Color"]       = setter_color4      <Rope, &Rope::Color>();

    SetterTable["Rod"]["LineWidth"] = setter_number<Rod, &Rod::LineWidth>();
    SetterTable["Rod"]["Color"]     = setter_color4<Rod, &Rod::Color>();

    SetterTable["Motor"]["DriveVelocity"] = setter_method_float<Motor, &Motor::setDriveVelocity>();
    SetterTable["Motor"]["MaxForce"]      = setter_method_float<Motor, &Motor::setMaxForce>();
}

// ==================== Setter: Sound, CharacterSetting, AppImage, Script ====================
void LuauEngine::InitSetterTable_Misc() {
    SetterTable["Sound"]["Looped"] = setter_method_bool <Sound, &Sound::setLooping>();
    SetterTable["Sound"]["Volume"] = setter_method_float<Sound, &Sound::setVolume>();

    SetterTable["CharacterSetting"]["JumpPower"]    = setter_number <CharacterSetting, &CharacterSetting::jumpPower>();
    SetterTable["CharacterSetting"]["MoveSpeed"]    = setter_number <CharacterSetting, &CharacterSetting::moveSpeed>();
    SetterTable["CharacterSetting"]["FacePath"]     = setter_string <CharacterSetting, &CharacterSetting::facePath>();
    SetterTable["CharacterSetting"]["HeadColor"]    = setter_color4 <CharacterSetting, &CharacterSetting::headColor>();
    SetterTable["CharacterSetting"]["TorsoColor"]   = setter_color4 <CharacterSetting, &CharacterSetting::torsoColor>();
    SetterTable["CharacterSetting"]["LeftArmColor"]  = setter_color4<CharacterSetting, &CharacterSetting::leftArmColor>();
    SetterTable["CharacterSetting"]["RightArmColor"] = setter_color4<CharacterSetting, &CharacterSetting::rightArmColor>();
    SetterTable["CharacterSetting"]["LeftLegColor"]  = setter_color4<CharacterSetting, &CharacterSetting::leftLegColor>();
    SetterTable["CharacterSetting"]["RightLegColor"] = setter_color4<CharacterSetting, &CharacterSetting::rightLegColor>();

    SetterTable["AppImage"]["IconPath"] = setter_string<AppImage, &AppImage::iconPath>();

    SetterTable["Script"]["Enabled"] = setter_bool  <Script, &Script::Enabled>();
    SetterTable["Script"]["Path"]    = setter_string<Script, &Script::Path>();
}


// ==================== Getter: GUI ====================
void LuauEngine::InitDispatchTable_GUI() {
    // --- ScreenGuiObject ---
    DispatchTable["ScreenGuiObject"]["Active"]          = getter_bool          <ScreenGuiObject, &ScreenGuiObject::Active>();
    DispatchTable["ScreenGuiObject"]["Visible"]         = getter_bool          <ScreenGuiObject, &ScreenGuiObject::Visible>();
    DispatchTable["ScreenGuiObject"]["ZIndex"]          = getter_int           <ScreenGuiObject, &ScreenGuiObject::ZIndex>();
    DispatchTable["ScreenGuiObject"]["Position"]        = getter_vec2          <ScreenGuiObject, &ScreenGuiObject::Position>();
    DispatchTable["ScreenGuiObject"]["Size"]            = getter_vec2          <ScreenGuiObject, &ScreenGuiObject::Size>();
    DispatchTable["ScreenGuiObject"]["BackgroundColor"] = getter_color4        <ScreenGuiObject, &ScreenGuiObject::BackgroundColor>();
    DispatchTable["ScreenGuiObject"]["Transparency"]    = getter_method_number <ScreenGuiObject, &ScreenGuiObject::getTransparency>();
    // Norm uses string conversion — custom lambda
    DispatchTable["ScreenGuiObject"]["Norm"] = [](lua_State* L, Instance* obj) {
        lua_pushstring(L, normToStr(static_cast<ScreenGuiObject*>(obj)->NormType));
        return 1;
    };

    // --- GuiButton ---
    DispatchTable["GuiButton"]["Activated"] = getter_signal<GuiButton, &GuiButton::Activated>();

    // --- TextLabel ---
    DispatchTable["TextLabel"]["Text"]      = getter_string<TextLabel, &TextLabel::Text>();
    DispatchTable["TextLabel"]["TextColor"] = getter_color4<TextLabel, &TextLabel::TextColor>();

    // --- TextButton ---
    DispatchTable["TextButton"]["Text"]      = getter_string<TextButton, &TextButton::Text>();
    DispatchTable["TextButton"]["TextColor"] = getter_color4<TextButton, &TextButton::TextColor>();

    // --- WorldGuiObject ---
    DispatchTable["WorldGuiObject"]["Active"]          = getter_bool          <WorldGuiObject, &WorldGuiObject::Active>();
    DispatchTable["WorldGuiObject"]["Visible"]         = getter_bool          <WorldGuiObject, &WorldGuiObject::Visible>();
    DispatchTable["WorldGuiObject"]["ZIndex"]          = getter_int           <WorldGuiObject, &WorldGuiObject::ZIndex>();
    DispatchTable["WorldGuiObject"]["Size"]            = getter_vec2          <WorldGuiObject, &WorldGuiObject::Size>();
    DispatchTable["WorldGuiObject"]["BackgroundColor"] = getter_color4        <WorldGuiObject, &WorldGuiObject::BackgroundColor>();
    DispatchTable["WorldGuiObject"]["Transparency"]    = getter_method_number <WorldGuiObject, &WorldGuiObject::getTransparency>();
    DispatchTable["WorldGuiObject"]["Norm"] = [](lua_State* L, Instance* obj) {
        lua_pushstring(L, normToStr(static_cast<WorldGuiObject*>(obj)->NormType));
        return 1;
    };

    // --- SurfaceGui ---
    DispatchTable["SurfaceGui"]["Face"] = [](lua_State* L, Instance* obj) {
        lua_pushstring(L, faceToStr(static_cast<SurfaceGui*>(obj)->face));
        return 1;
    };

    // --- BillboardGui ---
    DispatchTable["BillboardGui"]["Mode"] = [](lua_State* L, Instance* obj) {
        auto m = static_cast<BillboardGui*>(obj)->Mode;
        lua_pushstring(L, m == BillboardMode::Focus ? "Focus" : "Parallel");
        return 1;
    };

    // --- ProximityPrompt ---
    DispatchTable["ProximityPrompt"]["KeyboardKeyCode"]       = getter_string<ProximityPrompt, &ProximityPrompt::KeyboardKeyCode>();
    DispatchTable["ProximityPrompt"]["HoldDuration"]          = getter_number<ProximityPrompt, &ProximityPrompt::HoldDuration>();
    DispatchTable["ProximityPrompt"]["MaxActivationDistance"] = getter_number<ProximityPrompt, &ProximityPrompt::MaxActivationDistance>();
    DispatchTable["ProximityPrompt"]["Enabled"]               = getter_bool  <ProximityPrompt, &ProximityPrompt::Enabled>();
    DispatchTable["ProximityPrompt"]["ActionText"]            = getter_string<ProximityPrompt, &ProximityPrompt::ActionText>();
    DispatchTable["ProximityPrompt"]["ObjectText"]            = getter_string<ProximityPrompt, &ProximityPrompt::ObjectText>();
    DispatchTable["ProximityPrompt"]["Triggered"]             = getter_signal<ProximityPrompt, &ProximityPrompt::Triggered>();
}

// ==================== Setter: GUI ====================
void LuauEngine::InitSetterTable_GUI() {
    // --- ScreenGuiObject ---
    SetterTable["ScreenGuiObject"]["Active"]          = setter_bool        <ScreenGuiObject, &ScreenGuiObject::Active>();
    SetterTable["ScreenGuiObject"]["Visible"]         = setter_bool        <ScreenGuiObject, &ScreenGuiObject::Visible>();
    SetterTable["ScreenGuiObject"]["ZIndex"]          = setter_int         <ScreenGuiObject, &ScreenGuiObject::ZIndex>();
    SetterTable["ScreenGuiObject"]["Position"]        = setter_vec2        <ScreenGuiObject, &ScreenGuiObject::Position>();
    SetterTable["ScreenGuiObject"]["Size"]            = setter_vec2        <ScreenGuiObject, &ScreenGuiObject::Size>();
    SetterTable["ScreenGuiObject"]["BackgroundColor"] = setter_color4      <ScreenGuiObject, &ScreenGuiObject::BackgroundColor>();
    SetterTable["ScreenGuiObject"]["Transparency"]    = setter_method_float<ScreenGuiObject, &ScreenGuiObject::setTransparency>();
    SetterTable["ScreenGuiObject"]["Norm"] = [](lua_State* L, Instance* obj) {
        static_cast<ScreenGuiObject*>(obj)->NormType = strToNorm(luaL_checkstring(L, 3));
        return 0;
    };

    // --- TextLabel ---
    SetterTable["TextLabel"]["Text"]      = setter_string<TextLabel, &TextLabel::Text>();
    SetterTable["TextLabel"]["TextColor"] = setter_color4<TextLabel, &TextLabel::TextColor>();

    // --- TextButton ---
    SetterTable["TextButton"]["Text"]      = setter_string<TextButton, &TextButton::Text>();
    SetterTable["TextButton"]["TextColor"] = setter_color4<TextButton, &TextButton::TextColor>();

    // --- WorldGuiObject ---
    SetterTable["WorldGuiObject"]["Active"]          = setter_bool        <WorldGuiObject, &WorldGuiObject::Active>();
    SetterTable["WorldGuiObject"]["Visible"]         = setter_bool        <WorldGuiObject, &WorldGuiObject::Visible>();
    SetterTable["WorldGuiObject"]["ZIndex"]          = setter_int         <WorldGuiObject, &WorldGuiObject::ZIndex>();
    SetterTable["WorldGuiObject"]["Size"]            = setter_vec2        <WorldGuiObject, &WorldGuiObject::Size>();
    SetterTable["WorldGuiObject"]["BackgroundColor"] = setter_color4      <WorldGuiObject, &WorldGuiObject::BackgroundColor>();
    SetterTable["WorldGuiObject"]["Transparency"]    = setter_method_float<WorldGuiObject, &WorldGuiObject::setTransparency>();
    SetterTable["WorldGuiObject"]["Norm"] = [](lua_State* L, Instance* obj) {
        static_cast<WorldGuiObject*>(obj)->NormType = strToNorm(luaL_checkstring(L, 3));
        return 0;
    };

    // --- SurfaceGui ---
    SetterTable["SurfaceGui"]["Face"] = [](lua_State* L, Instance* obj) {
        static_cast<SurfaceGui*>(obj)->face = strToFace(luaL_checkstring(L, 3));
        return 0;
    };

    // --- BillboardGui ---
    SetterTable["BillboardGui"]["Mode"] = [](lua_State* L, Instance* obj) {
        std::string_view v = luaL_checkstring(L, 3);
        static_cast<BillboardGui*>(obj)->Mode =
            (v == "Focus") ? BillboardMode::Focus : BillboardMode::Parallel;
        return 0;
    };

    // --- ProximityPrompt ---
    SetterTable["ProximityPrompt"]["KeyboardKeyCode"]       = setter_string<ProximityPrompt, &ProximityPrompt::KeyboardKeyCode>();
    SetterTable["ProximityPrompt"]["HoldDuration"]          = setter_number<ProximityPrompt, &ProximityPrompt::HoldDuration>();
    SetterTable["ProximityPrompt"]["MaxActivationDistance"] = setter_number<ProximityPrompt, &ProximityPrompt::MaxActivationDistance>();
    SetterTable["ProximityPrompt"]["Enabled"]               = setter_bool  <ProximityPrompt, &ProximityPrompt::Enabled>();
    SetterTable["ProximityPrompt"]["ActionText"]            = setter_string<ProximityPrompt, &ProximityPrompt::ActionText>();
    SetterTable["ProximityPrompt"]["ObjectText"]            = setter_string<ProximityPrompt, &ProximityPrompt::ObjectText>();
}