#include "include/Core/LuauEngine.hpp"
#include "include/Core/Physics.hpp"
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

// ==================== Getter: Instance, BaseCube ====================
void LuauEngine::InitDispatchTable_Base() {
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
    DispatchTable["Instance"]["FindChild"] = [](lua_State* L, Instance* obj) {
        std::cout << "Accessing FindChild method\n";
        auto* userdata = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
        new (userdata) std::weak_ptr<Instance>(obj->shared_from_this());
        luaL_getmetatable(L, RCBN_INST_METATABLE);
        lua_setmetatable(L, -2);
        lua_pushcclosure(L, instance_find_child_closure, "FindChild", 1);
        return 1;
    };
    DispatchTable["Instance"]["GetChildren"] = [](lua_State* L, Instance* obj) {
        auto* ud = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
        new (ud) std::weak_ptr<Instance>(obj->shared_from_this());
        luaL_getmetatable(L, RCBN_INST_METATABLE);
        lua_setmetatable(L, -2);
        lua_pushcclosure(L, instance_get_children_closure, "GetChildren", 1);
        return 1;
    };
    DispatchTable["Instance"]["IsA"] = [](lua_State* L, Instance* obj) {
        auto* ud = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
        new (ud) std::weak_ptr<Instance>(obj->shared_from_this());
        luaL_getmetatable(L, RCBN_INST_METATABLE);
        lua_setmetatable(L, -2);
        lua_pushcclosure(L, instance_is_a_closure, "IsA", 1);
        return 1;
    };
    DispatchTable["Instance"]["Destroy"] = [](lua_State* L, Instance* obj) {
        auto* ud = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
        new (ud) std::weak_ptr<Instance>(obj->shared_from_this());
        luaL_getmetatable(L, RCBN_INST_METATABLE);
        lua_setmetatable(L, -2);
        lua_pushcclosure(L, instance_destroy_closure, "Destroy", 1);
        return 1;
    };

    DispatchTable["BaseCube"]["Position"] = [](lua_State* L, Instance* obj) {
        Vector3* v = (Vector3*)lua_newuserdata(L, sizeof(Vector3));
        *v = static_cast<BaseCube*>(obj)->Position;
        luaL_getmetatable(L, RCBN_VEC3_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    };
    DispatchTable["BaseCube"]["Size"] = [](lua_State* L, Instance* obj) {
        Vector3* v = (Vector3*)lua_newuserdata(L, sizeof(Vector3));
        *v = static_cast<BaseCube*>(obj)->Size;
        luaL_getmetatable(L, RCBN_VEC3_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    };
    DispatchTable["BaseCube"]["Color"] = [](lua_State* L, Instance* obj) {
        Color4* c = (Color4*)lua_newuserdata(L, sizeof(Color4));
        *c = static_cast<BaseCube*>(obj)->Color;
        luaL_getmetatable(L, RCBN_COLOR4_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    };
    DispatchTable["BaseCube"]["Anchored"] = [](lua_State* L, Instance* obj) {
        lua_pushboolean(L, static_cast<BaseCube*>(obj)->Anchored);
        return 1;
    };
    DispatchTable["BaseCube"]["CanCollide"] = [](lua_State* L, Instance* obj) {
        lua_pushboolean(L, static_cast<BaseCube*>(obj)->CanCollide);
        return 1;
    };
    DispatchTable["BaseCube"]["Velocity"] = [](lua_State* L, Instance* obj) {
        auto* cube = static_cast<BaseCube*>(obj);
        Vector3* v = (Vector3*)lua_newuserdata(L, sizeof(Vector3));
        *v = Vector3(0.0f, 0.0f, 0.0f);
        if (cube->actor) {
            auto* dyn = cube->actor->is<physx::PxRigidDynamic>();
            if (dyn) {
                physx::PxVec3 vel = dyn->getLinearVelocity();
                *v = Vector3(vel.x, vel.y, vel.z);
            }
        }
        luaL_getmetatable(L, RCBN_VEC3_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    };
}

// ==================== Getter: Workspace, Decal, Lighting ====================
void LuauEngine::InitDispatchTable_World() {
    DispatchTable["Workspace"]["Gravity"] = [](lua_State* L, Instance* obj) {
        Vector3* v = (Vector3*)lua_newuserdata(L, sizeof(Vector3));
        *v = static_cast<Workspace*>(obj)->Gravity;
        luaL_getmetatable(L, RCBN_VEC3_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    };
    DispatchTable["Workspace"]["Raycast"] = [](lua_State* L, Instance* obj) {
        auto* userdata = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
        new (userdata) std::weak_ptr<Instance>(obj->shared_from_this());
        luaL_getmetatable(L, RCBN_INST_METATABLE);
        lua_setmetatable(L, -2);
        lua_pushcclosure(L, workspace_raycast_closure, "Raycast", 1);
        return 1;
    };

    DispatchTable["Decal"]["TextureID"] = [](lua_State* L, Instance* obj) {
        lua_pushnumber(L, static_cast<Decal*>(obj)->TextureID);
        return 1;
    };
    DispatchTable["Decal"]["Face"] = [](lua_State* L, Instance* obj) {
        lua_pushnumber(L, (int)static_cast<Decal*>(obj)->face);
        return 1;
    };

    DispatchTable["Lighting"]["Brightness"] = [](lua_State* L, Instance* obj) {
        lua_pushnumber(L, static_cast<Lighting*>(obj)->brightness);
        return 1;
    };
}

// ==================== Getter: Rope, Rod, Weld, Motor ====================
void LuauEngine::InitDispatchTable_Physics() {
    DispatchTable["Rope"]["MaxDistance"] = [](lua_State* L, Instance* obj) {
        lua_pushnumber(L, static_cast<Rope*>(obj)->MaxDistance);
        return 1;
    };
    DispatchTable["Rope"]["Stiffness"] = [](lua_State* L, Instance* obj) {
        lua_pushnumber(L, static_cast<Rope*>(obj)->Stiffness);
        return 1;
    };
    DispatchTable["Rope"]["Damping"] = [](lua_State* L, Instance* obj) {
        lua_pushnumber(L, static_cast<Rope*>(obj)->Damping);
        return 1;
    };
    DispatchTable["Rope"]["LineWidth"] = [](lua_State* L, Instance* obj) {
        lua_pushnumber(L, static_cast<Rope*>(obj)->LineWidth);
        return 1;
    };
    DispatchTable["Rope"]["Color"] = [](lua_State* L, Instance* obj) {
        Color4* c = (Color4*)lua_newuserdata(L, sizeof(Color4));
        *c = static_cast<Rope*>(obj)->Color;
        luaL_getmetatable(L, RCBN_COLOR4_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    };

    DispatchTable["Rod"]["LineWidth"] = [](lua_State* L, Instance* obj) {
        lua_pushnumber(L, static_cast<Rod*>(obj)->LineWidth);
        return 1;
    };
    DispatchTable["Rod"]["Color"] = [](lua_State* L, Instance* obj) {
        Color4* c = (Color4*)lua_newuserdata(L, sizeof(Color4));
        *c = static_cast<Rod*>(obj)->Color;
        luaL_getmetatable(L, RCBN_COLOR4_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    };

    DispatchTable["Weld"]["Cube0"] = [](lua_State* L, Instance* obj) {
        lua_pushstring(L, static_cast<Weld*>(obj)->m_cube0Name.c_str());
        return 1;
    };
    DispatchTable["Weld"]["Cube1"] = [](lua_State* L, Instance* obj) {
        lua_pushstring(L, static_cast<Weld*>(obj)->m_cube1Name.c_str());
        return 1;
    };

    DispatchTable["Motor"]["DriveVelocity"] = [](lua_State* L, Instance* obj) {
        lua_pushnumber(L, static_cast<Motor*>(obj)->DriveVelocity);
        return 1;
    };
    DispatchTable["Motor"]["MaxForce"] = [](lua_State* L, Instance* obj) {
        lua_pushnumber(L, static_cast<Motor*>(obj)->MaxForce);
        return 1;
    };
}

// ==================== Getter: Sound, CharacterSetting, AppImage, Script ====================
void LuauEngine::InitDispatchTable_Misc() {
    DispatchTable["Sound"]["IsPlaying"] = [](lua_State* L, Instance* obj) {
        lua_pushboolean(L, static_cast<Sound*>(obj)->isPlaying());
        return 1;
    };
    DispatchTable["Sound"]["Looped"] = [](lua_State* L, Instance* obj) {
        lua_pushboolean(L, static_cast<Sound*>(obj)->isLooping());
        return 1;
    };
    DispatchTable["Sound"]["Volume"] = [](lua_State* L, Instance* obj) {
        lua_pushnumber(L, static_cast<Sound*>(obj)->getVolume());
        return 1;
    };
    DispatchTable["Sound"]["Play"] = [](lua_State* L, Instance* obj) {
        auto* ud = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
        new (ud) std::weak_ptr<Instance>(obj->shared_from_this());
        luaL_getmetatable(L, RCBN_INST_METATABLE);
        lua_setmetatable(L, -2);
        lua_pushcclosure(L, sound_play_closure, "Play", 1);
        return 1;
    };
    DispatchTable["Sound"]["Stop"] = [](lua_State* L, Instance* obj) {
        auto* ud = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
        new (ud) std::weak_ptr<Instance>(obj->shared_from_this());
        luaL_getmetatable(L, RCBN_INST_METATABLE);
        lua_setmetatable(L, -2);
        lua_pushcclosure(L, sound_stop_closure, "Stop", 1);
        return 1;
    };

    DispatchTable["CharacterSetting"]["JumpPower"] = [](lua_State* L, Instance* obj) {
        lua_pushnumber(L, static_cast<CharacterSetting*>(obj)->jumpPower);
        return 1;
    };
    DispatchTable["CharacterSetting"]["MoveSpeed"] = [](lua_State* L, Instance* obj) {
        lua_pushnumber(L, static_cast<CharacterSetting*>(obj)->moveSpeed);
        return 1;
    };
    DispatchTable["CharacterSetting"]["FacePath"] = [](lua_State* L, Instance* obj) {
        lua_pushstring(L, static_cast<CharacterSetting*>(obj)->facePath.c_str());
        return 1;
    };
    DispatchTable["CharacterSetting"]["HeadColor"] = [](lua_State* L, Instance* obj) {
        Color4* c = (Color4*)lua_newuserdata(L, sizeof(Color4));
        *c = static_cast<CharacterSetting*>(obj)->headColor;
        luaL_getmetatable(L, RCBN_COLOR4_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    };
    DispatchTable["CharacterSetting"]["TorsoColor"] = [](lua_State* L, Instance* obj) {
        Color4* c = (Color4*)lua_newuserdata(L, sizeof(Color4));
        *c = static_cast<CharacterSetting*>(obj)->torsoColor;
        luaL_getmetatable(L, RCBN_COLOR4_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    };
    DispatchTable["CharacterSetting"]["LeftArmColor"] = [](lua_State* L, Instance* obj) {
        Color4* c = (Color4*)lua_newuserdata(L, sizeof(Color4));
        *c = static_cast<CharacterSetting*>(obj)->leftArmColor;
        luaL_getmetatable(L, RCBN_COLOR4_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    };
    DispatchTable["CharacterSetting"]["RightArmColor"] = [](lua_State* L, Instance* obj) {
        Color4* c = (Color4*)lua_newuserdata(L, sizeof(Color4));
        *c = static_cast<CharacterSetting*>(obj)->rightArmColor;
        luaL_getmetatable(L, RCBN_COLOR4_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    };
    DispatchTable["CharacterSetting"]["LeftLegColor"] = [](lua_State* L, Instance* obj) {
        Color4* c = (Color4*)lua_newuserdata(L, sizeof(Color4));
        *c = static_cast<CharacterSetting*>(obj)->leftLegColor;
        luaL_getmetatable(L, RCBN_COLOR4_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    };
    DispatchTable["CharacterSetting"]["RightLegColor"] = [](lua_State* L, Instance* obj) {
        Color4* c = (Color4*)lua_newuserdata(L, sizeof(Color4));
        *c = static_cast<CharacterSetting*>(obj)->rightLegColor;
        luaL_getmetatable(L, RCBN_COLOR4_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    };

    DispatchTable["AppImage"]["IconPath"] = [](lua_State* L, Instance* obj) {
        lua_pushstring(L, static_cast<AppImage*>(obj)->iconPath.c_str());
        return 1;
    };

    DispatchTable["Script"]["Enabled"] = [](lua_State* L, Instance* obj) {
        lua_pushboolean(L, static_cast<Script*>(obj)->Enabled);
        return 1;
    };
    DispatchTable["Script"]["Path"] = [](lua_State* L, Instance* obj) {
        lua_pushstring(L, static_cast<Script*>(obj)->Path.c_str());
        return 1;
    };
    DispatchTable["Script"]["Source"] = [](lua_State* L, Instance* obj) {
        lua_pushstring(L, static_cast<Script*>(obj)->Source.c_str());
        return 1;
    };
}

// ==================== Setter: Instance, BaseCube ====================
void LuauEngine::InitSetterTable_Base() {
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

    SetterTable["BaseCube"]["Position"] = [](lua_State* L, Instance* obj) {
        auto cube = static_cast<BaseCube*>(obj);
        if (lua_isuserdata(L, 3)) {
            Vector3* newPos = (Vector3*)luaL_checkudata(L, 3, RCBN_VEC3_METATABLE);
            cube->Position = *newPos;
            if (cube->actor) {
                physx::PxTransform pose = cube->actor->getGlobalPose();
                pose.p = physx::PxVec3(newPos->x, newPos->y, newPos->z);
                cube->actor->setGlobalPose(pose);
            }
        } else {
            std::cerr << "Expected a Vector3 userdata for Position\n";
        }
        return 0;
    };
    SetterTable["BaseCube"]["Size"] = [](lua_State* L, Instance* obj) {
        Vector3* v = (Vector3*)luaL_checkudata(L, 3, RCBN_VEC3_METATABLE);
        static_cast<BaseCube*>(obj)->setSize(*v);
        return 0;
    };
    SetterTable["BaseCube"]["Color"] = [](lua_State* L, Instance* obj) {
        if (lua_isuserdata(L, 3)) {
            Color4* newColor = (Color4*)luaL_checkudata(L, 3, RCBN_COLOR4_METATABLE);
            static_cast<BaseCube*>(obj)->Color = *newColor;
        } else {
            std::cerr << "Expected a Color4 userdata for Color\n";
        }
        return 0;
    };
    SetterTable["BaseCube"]["Anchored"] = [](lua_State* L, Instance* obj) {
        static_cast<BaseCube*>(obj)->setAnchored(lua_toboolean(L, 3) != 0);
        return 0;
    };
    SetterTable["BaseCube"]["CanCollide"] = [](lua_State* L, Instance* obj) {
        static_cast<BaseCube*>(obj)->CanCollide = lua_toboolean(L, 3) != 0;
        return 0;
    };
}

// ==================== Setter: Workspace, Decal, Lighting ====================
void LuauEngine::InitSetterTable_World() {
    SetterTable["Workspace"]["Gravity"] = [](lua_State* L, Instance* obj) {
        auto* ws = static_cast<Workspace*>(obj);
        Vector3* v = (Vector3*)luaL_checkudata(L, 3, RCBN_VEC3_METATABLE);
        ws->Gravity = *v;
        if (ws->getPhysicsEngine()) ws->getPhysicsEngine()->setGravity(*v);
        return 0;
    };

    SetterTable["Decal"]["TextureID"] = [](lua_State* L, Instance* obj) {
        static_cast<Decal*>(obj)->TextureID = (unsigned int)luaL_checknumber(L, 3);
        return 0;
    };
    SetterTable["Decal"]["Face"] = [](lua_State* L, Instance* obj) {
        static_cast<Decal*>(obj)->face = (Face)(int)luaL_checknumber(L, 3);
        return 0;
    };

    SetterTable["Lighting"]["Brightness"] = [](lua_State* L, Instance* obj) {
        static_cast<Lighting*>(obj)->brightness = (float)luaL_checknumber(L, 3);
        return 0;
    };
}

// ==================== Setter: Rope, Rod, Motor ====================
void LuauEngine::InitSetterTable_Physics() {
    SetterTable["Rope"]["MaxDistance"] = [](lua_State* L, Instance* obj) {
        static_cast<Rope*>(obj)->setMaxDistance((float)luaL_checknumber(L, 3));
        return 0;
    };
    SetterTable["Rope"]["Stiffness"] = [](lua_State* L, Instance* obj) {
        static_cast<Rope*>(obj)->setStiffness((float)luaL_checknumber(L, 3));
        return 0;
    };
    SetterTable["Rope"]["Damping"] = [](lua_State* L, Instance* obj) {
        static_cast<Rope*>(obj)->setDamping((float)luaL_checknumber(L, 3));
        return 0;
    };
    SetterTable["Rope"]["LineWidth"] = [](lua_State* L, Instance* obj) {
        static_cast<Rope*>(obj)->LineWidth = (float)luaL_checknumber(L, 3);
        return 0;
    };
    SetterTable["Rope"]["Color"] = [](lua_State* L, Instance* obj) {
        Color4* c = (Color4*)luaL_checkudata(L, 3, RCBN_COLOR4_METATABLE);
        static_cast<Rope*>(obj)->Color = *c;
        return 0;
    };

    SetterTable["Rod"]["LineWidth"] = [](lua_State* L, Instance* obj) {
        static_cast<Rod*>(obj)->LineWidth = (float)luaL_checknumber(L, 3);
        return 0;
    };
    SetterTable["Rod"]["Color"] = [](lua_State* L, Instance* obj) {
        Color4* c = (Color4*)luaL_checkudata(L, 3, RCBN_COLOR4_METATABLE);
        static_cast<Rod*>(obj)->Color = *c;
        return 0;
    };

    SetterTable["Motor"]["DriveVelocity"] = [](lua_State* L, Instance* obj) {
        static_cast<Motor*>(obj)->setDriveVelocity((float)luaL_checknumber(L, 3));
        return 0;
    };
    SetterTable["Motor"]["MaxForce"] = [](lua_State* L, Instance* obj) {
        static_cast<Motor*>(obj)->setMaxForce((float)luaL_checknumber(L, 3));
        return 0;
    };
}

// ==================== Setter: Sound, CharacterSetting, AppImage, Script ====================
void LuauEngine::InitSetterTable_Misc() {
    SetterTable["Sound"]["Looped"] = [](lua_State* L, Instance* obj) {
        static_cast<Sound*>(obj)->setLooping(lua_toboolean(L, 3) != 0);
        return 0;
    };
    SetterTable["Sound"]["Volume"] = [](lua_State* L, Instance* obj) {
        static_cast<Sound*>(obj)->setVolume((float)luaL_checknumber(L, 3));
        return 0;
    };

    SetterTable["CharacterSetting"]["JumpPower"] = [](lua_State* L, Instance* obj) {
        static_cast<CharacterSetting*>(obj)->jumpPower = (float)luaL_checknumber(L, 3);
        return 0;
    };
    SetterTable["CharacterSetting"]["MoveSpeed"] = [](lua_State* L, Instance* obj) {
        static_cast<CharacterSetting*>(obj)->moveSpeed = (float)luaL_checknumber(L, 3);
        return 0;
    };
    SetterTable["CharacterSetting"]["FacePath"] = [](lua_State* L, Instance* obj) {
        static_cast<CharacterSetting*>(obj)->facePath = luaL_checkstring(L, 3);
        return 0;
    };
    SetterTable["CharacterSetting"]["HeadColor"] = [](lua_State* L, Instance* obj) {
        Color4* c = (Color4*)luaL_checkudata(L, 3, RCBN_COLOR4_METATABLE);
        static_cast<CharacterSetting*>(obj)->headColor = *c;
        return 0;
    };
    SetterTable["CharacterSetting"]["TorsoColor"] = [](lua_State* L, Instance* obj) {
        Color4* c = (Color4*)luaL_checkudata(L, 3, RCBN_COLOR4_METATABLE);
        static_cast<CharacterSetting*>(obj)->torsoColor = *c;
        return 0;
    };
    SetterTable["CharacterSetting"]["LeftArmColor"] = [](lua_State* L, Instance* obj) {
        Color4* c = (Color4*)luaL_checkudata(L, 3, RCBN_COLOR4_METATABLE);
        static_cast<CharacterSetting*>(obj)->leftArmColor = *c;
        return 0;
    };
    SetterTable["CharacterSetting"]["RightArmColor"] = [](lua_State* L, Instance* obj) {
        Color4* c = (Color4*)luaL_checkudata(L, 3, RCBN_COLOR4_METATABLE);
        static_cast<CharacterSetting*>(obj)->rightArmColor = *c;
        return 0;
    };
    SetterTable["CharacterSetting"]["LeftLegColor"] = [](lua_State* L, Instance* obj) {
        Color4* c = (Color4*)luaL_checkudata(L, 3, RCBN_COLOR4_METATABLE);
        static_cast<CharacterSetting*>(obj)->leftLegColor = *c;
        return 0;
    };
    SetterTable["CharacterSetting"]["RightLegColor"] = [](lua_State* L, Instance* obj) {
        Color4* c = (Color4*)luaL_checkudata(L, 3, RCBN_COLOR4_METATABLE);
        static_cast<CharacterSetting*>(obj)->rightLegColor = *c;
        return 0;
    };

    SetterTable["AppImage"]["IconPath"] = [](lua_State* L, Instance* obj) {
        static_cast<AppImage*>(obj)->iconPath = luaL_checkstring(L, 3);
        return 0;
    };

    SetterTable["Script"]["Enabled"] = [](lua_State* L, Instance* obj) {
        static_cast<Script*>(obj)->Enabled = lua_toboolean(L, 3) != 0;
        return 0;
    };
    SetterTable["Script"]["Path"] = [](lua_State* L, Instance* obj) {
        static_cast<Script*>(obj)->Path = luaL_checkstring(L, 3);
        return 0;
    };
}
