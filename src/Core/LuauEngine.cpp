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
    DispatchTable["Instance"]["Name"] = [](lua_State* L, Instance* obj) {
        std::cout << "Accessing Name of Instance: " << obj->Name << std::endl;
        lua_pushstring(L, obj->Name.c_str());
        return 1;
    };

    DispatchTable["Instance"]["FindChild"] = [](lua_State* L, Instance* obj) {
        std::cout << "Accessing FindChild method\n";
        // objをuserdataとしてスタックに積む（クロージャのupvalueとして使用）
        auto* userdata = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
        new (userdata) std::weak_ptr<Instance>(obj->shared_from_this());
        luaL_getmetatable(L, RCBN_INST_METATABLE);
        lua_setmetatable(L, -2);
        // C関数をクロージャとして作成（1つのupvalue）
        lua_pushcclosure(L, instance_find_child_closure, "FindChild", 1);
        return 1;
    };

    DispatchTable["BaseCube"]["Position"] = [](lua_State* L, Instance* obj) {
        auto* cube = static_cast<BaseCube*>(obj);
        Vector3* v = (Vector3*)lua_newuserdata(L, sizeof(Vector3));
        *v = cube->Position;
        luaL_getmetatable(L, RCBN_VEC3_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    };

    DispatchTable["BaseCube"]["Color"] = [](lua_State* L, Instance* obj) {
        auto* cube = static_cast<BaseCube*>(obj);
        Color4* c = (Color4*)lua_newuserdata(L, sizeof(Color4));
        *c = cube->Color;
        luaL_getmetatable(L, RCBN_COLOR4_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    };

    DispatchTable["Decal"]["TextureID"] = [](lua_State* L, Instance* obj) {
        auto decal = static_cast<Decal*>(obj);
        lua_pushnumber(L, decal->TextureID);
        return 1;
    };

    DispatchTable["Decal"]["Face"] = [](lua_State* L, Instance* obj) {
        auto decal = static_cast<Decal*>(obj);
        lua_pushnumber(L, (int)decal->face);
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

    // Instance
    DispatchTable["Instance"]["Parent"] = [](lua_State* L, Instance* obj) {
        auto parent = obj->Parent.lock();
        if (!parent) { lua_pushnil(L); return 1; }
        auto* ud = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
        new (ud) std::weak_ptr<Instance>(parent);
        luaL_getmetatable(L, RCBN_INST_METATABLE);
        lua_setmetatable(L, -2);
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

    // BaseCube
    DispatchTable["BaseCube"]["Size"] = [](lua_State* L, Instance* obj) {
        auto* cube = static_cast<BaseCube*>(obj);
        Vector3* v = (Vector3*)lua_newuserdata(L, sizeof(Vector3));
        *v = cube->Size;
        luaL_getmetatable(L, RCBN_VEC3_METATABLE);
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

    // Workspace
    DispatchTable["Workspace"]["Gravity"] = [](lua_State* L, Instance* obj) {
        auto* ws = static_cast<Workspace*>(obj);
        Vector3* v = (Vector3*)lua_newuserdata(L, sizeof(Vector3));
        *v = ws->Gravity;
        luaL_getmetatable(L, RCBN_VEC3_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    };

    // Motor
    DispatchTable["Motor"]["DriveVelocity"] = [](lua_State* L, Instance* obj) {
        lua_pushnumber(L, static_cast<Motor*>(obj)->DriveVelocity);
        return 1;
    };
    DispatchTable["Motor"]["MaxForce"] = [](lua_State* L, Instance* obj) {
        lua_pushnumber(L, static_cast<Motor*>(obj)->MaxForce);
        return 1;
    };

    // Sound
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

    // Lighting
    DispatchTable["Lighting"]["Brightness"] = [](lua_State* L, Instance* obj) {
        lua_pushnumber(L, static_cast<Lighting*>(obj)->brightness);
        return 1;
    };

    // Rope
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

    // Rod
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

    // Weld
    DispatchTable["Weld"]["Cube0"] = [](lua_State* L, Instance* obj) {
        lua_pushstring(L, static_cast<Weld*>(obj)->m_cube0Name.c_str());
        return 1;
    };
    DispatchTable["Weld"]["Cube1"] = [](lua_State* L, Instance* obj) {
        lua_pushstring(L, static_cast<Weld*>(obj)->m_cube1Name.c_str());
        return 1;
    };

    // CharacterSetting
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

    // AppImage
    DispatchTable["AppImage"]["IconPath"] = [](lua_State* L, Instance* obj) {
        lua_pushstring(L, static_cast<AppImage*>(obj)->iconPath.c_str());
        return 1;
    };

    // Script
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

void LuauEngine::InitSetterTable() {
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
        // userdataからVector3を取得してセットするロジックをここに実装
         if (lua_isuserdata(L, 3)) {
            Vector3* newPos = (Vector3*)luaL_checkudata(L, 3, RCBN_VEC3_METATABLE);
            cube->Position = *newPos;
            
            // 物理エンジン側にも位置を反映させる
            if (cube->actor) {
                physx::PxTransform pose = cube->actor->getGlobalPose();
                pose.p = physx::PxVec3(newPos->x, newPos->y, newPos->z);
                cube->actor->setGlobalPose(pose);
            }

            // std::cout << "Setting Position of BaseCube to " << cube->cframe.Position.toString() << std::endl;
        } else {
            std::cerr << "Expected a Vector3 userdata for Position\n";
        }
        return 0;
    };

    SetterTable["BaseCube"]["Color"] = [](lua_State* L, Instance* obj) {
        auto cube = static_cast<BaseCube*>(obj);
        // userdataからColor4を取得してセットするロジックをここに実装
        if (lua_isuserdata(L, 3)) {
            Color4* newColor = (Color4*)luaL_checkudata(L, 3, RCBN_COLOR4_METATABLE);
            cube->Color = *newColor;
            // std::cout << "Setting Color of BaseCube to " << cube->Color.toString() << std::endl;
        } else {
            std::cerr << "Expected a Color4 userdata for Color\n";
        }
        return 0;
    };

    SetterTable["Decal"]["TextureID"] = [](lua_State* L, Instance* obj) {
        auto decal = static_cast<Decal*>(obj);
        decal->TextureID = (unsigned int)luaL_checknumber(L, 3);
        return 0;
    };

    SetterTable["Decal"]["Face"] = [](lua_State* L, Instance* obj) {
        auto decal = static_cast<Decal*>(obj);
        decal->face = (Face)(int)luaL_checknumber(L, 3);
        return 0;
    };

    // BaseCube
    SetterTable["BaseCube"]["Size"] = [](lua_State* L, Instance* obj) {
        auto* cube = static_cast<BaseCube*>(obj);
        Vector3* v = (Vector3*)luaL_checkudata(L, 3, RCBN_VEC3_METATABLE);
        cube->setSize(*v);
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

    // Workspace
    SetterTable["Workspace"]["Gravity"] = [](lua_State* L, Instance* obj) {
        auto* ws = static_cast<Workspace*>(obj);
        Vector3* v = (Vector3*)luaL_checkudata(L, 3, RCBN_VEC3_METATABLE);
        ws->Gravity = *v;
        if (ws->getPhysicsEngine()) ws->getPhysicsEngine()->setGravity(*v);
        return 0;
    };

    // Motor
    SetterTable["Motor"]["DriveVelocity"] = [](lua_State* L, Instance* obj) {
        static_cast<Motor*>(obj)->setDriveVelocity((float)luaL_checknumber(L, 3));
        return 0;
    };
    SetterTable["Motor"]["MaxForce"] = [](lua_State* L, Instance* obj) {
        static_cast<Motor*>(obj)->setMaxForce((float)luaL_checknumber(L, 3));
        return 0;
    };

    // Sound
    SetterTable["Sound"]["Looped"] = [](lua_State* L, Instance* obj) {
        static_cast<Sound*>(obj)->setLooping(lua_toboolean(L, 3) != 0);
        return 0;
    };
    SetterTable["Sound"]["Volume"] = [](lua_State* L, Instance* obj) {
        static_cast<Sound*>(obj)->setVolume((float)luaL_checknumber(L, 3));
        return 0;
    };

    // Lighting
    SetterTable["Lighting"]["Brightness"] = [](lua_State* L, Instance* obj) {
        static_cast<Lighting*>(obj)->brightness = (float)luaL_checknumber(L, 3);
        return 0;
    };

    // Rope
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

    // Rod
    SetterTable["Rod"]["LineWidth"] = [](lua_State* L, Instance* obj) {
        static_cast<Rod*>(obj)->LineWidth = (float)luaL_checknumber(L, 3);
        return 0;
    };
    SetterTable["Rod"]["Color"] = [](lua_State* L, Instance* obj) {
        Color4* c = (Color4*)luaL_checkudata(L, 3, RCBN_COLOR4_METATABLE);
        static_cast<Rod*>(obj)->Color = *c;
        return 0;
    };

    // CharacterSetting
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

    // AppImage
    SetterTable["AppImage"]["IconPath"] = [](lua_State* L, Instance* obj) {
        static_cast<AppImage*>(obj)->iconPath = luaL_checkstring(L, 3);
        return 0;
    };

    // Script
    SetterTable["Script"]["Enabled"] = [](lua_State* L, Instance* obj) {
        static_cast<Script*>(obj)->Enabled = lua_toboolean(L, 3) != 0;
        return 0;
    };
    SetterTable["Script"]["Path"] = [](lua_State* L, Instance* obj) {
        static_cast<Script*>(obj)->Path = luaL_checkstring(L, 3);
        return 0;
    };
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

// ==================== Vector3 Methods ====================
int LuauEngine::vec3_constructor(lua_State* L) {
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float z = (float)luaL_checknumber(L, 3);

    Vector3* vec = (Vector3*)lua_newuserdata(L, sizeof(Vector3));
    *vec = Vector3(x, y, z);

    luaL_getmetatable(L, RCBN_VEC3_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

// int LuauEngine::vec3_zeroconstructor(lua_State* L) {
//     Vector3* vec = (Vector3*)lua_newuserdata(L, sizeof(Vector3));
//     *vec = Vector3(0.0f, 0.0f, 0.0f);

//     luaL_getmetatable(L, RCBN_VEC3_METATABLE);
//     lua_setmetatable(L, -2);
//     return 1;
// }

int LuauEngine::vec3_index(lua_State* L) {
    Vector3* vec = (Vector3*)luaL_checkudata(L, 1, RCBN_VEC3_METATABLE);
    std::string_view key = luaL_checkstring(L, 2);

    if (key == "x") {
        lua_pushnumber(L, vec->x);
        return 1;
    } else if (key == "y") {
        lua_pushnumber(L, vec->y);
        return 1;
    } else if (key == "z") {
        lua_pushnumber(L, vec->z);
        return 1;
    } else if (key == "length") {
        lua_pushcfunction(L, [](lua_State* L) -> int {
            Vector3* v = (Vector3*)luaL_checkudata(L, 1, RCBN_VEC3_METATABLE);
            lua_pushnumber(L, v->length());
            return 1;
        }, "length");
        return 1;
    } else if (key == "normalize") {
        lua_pushcfunction(L, [](lua_State* L) -> int {
            Vector3* v = (Vector3*)luaL_checkudata(L, 1, RCBN_VEC3_METATABLE);
            Vector3 normalized = v->normalize();
            Vector3* result = (Vector3*)lua_newuserdata(L, sizeof(Vector3));
            *result = normalized;
            luaL_getmetatable(L, RCBN_VEC3_METATABLE);
            lua_setmetatable(L, -2);
            return 1;
        }, "normalize");
        return 1;
    }

    return 0;
}

int LuauEngine::vec3_newindex(lua_State* L) {
    Vector3* vec = (Vector3*)luaL_checkudata(L, 1, RCBN_VEC3_METATABLE);
    std::string_view key = luaL_checkstring(L, 2);
    float value = (float)luaL_checknumber(L, 3);

    if (key == "x") {
        vec->x = value;
    } else if (key == "y") {
        vec->y = value;
    } else if (key == "z") {
        vec->z = value;
    }

    return 0;
}

int LuauEngine::vec3_tostring(lua_State* L) {
    Vector3* vec = (Vector3*)luaL_checkudata(L, 1, RCBN_VEC3_METATABLE);
    std::string str = "Vector3(" + vec->toString() + ")";
    lua_pushstring(L, str.c_str());
    return 1;
}

// ==================== Color4 Methods ====================
int LuauEngine::color4_constructor(lua_State* L) {
    float r = (float)luaL_checknumber(L, 1);
    float g = (float)luaL_checknumber(L, 2);
    float b = (float)luaL_checknumber(L, 3);
    float a = lua_isnumber(L, 4) ? (float)lua_tonumber(L, 4) : 1.0f;

    Color4* color = (Color4*)lua_newuserdata(L, sizeof(Color4));
    *color = Color4(r, g, b, a);

    luaL_getmetatable(L, RCBN_COLOR4_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

int LuauEngine::color4_index(lua_State* L) {
    Color4* color = (Color4*)luaL_checkudata(L, 1, RCBN_COLOR4_METATABLE);
    std::string_view key = luaL_checkstring(L, 2);

    if (key == "r") {
        lua_pushnumber(L, color->r);
        return 1;
    } else if (key == "g") {
        lua_pushnumber(L, color->g);
        return 1;
    } else if (key == "b") {
        lua_pushnumber(L, color->b);
        return 1;
    } else if (key == "a") {
        lua_pushnumber(L, color->a);
        return 1;
    }

    return 0;
}

int LuauEngine::color4_newindex(lua_State* L) {
    Color4* color = (Color4*)luaL_checkudata(L, 1, RCBN_COLOR4_METATABLE);
    std::string_view key = luaL_checkstring(L, 2);
    float value = (float)luaL_checknumber(L, 3);

    if (key == "r") {
        color->r = value;
    } else if (key == "g") {
        color->g = value;
    } else if (key == "b") {
        color->b = value;
    } else if (key == "a") {
        color->a = value;
    }

    return 0;
}

int LuauEngine::color4_tostring(lua_State* L) {
    Color4* color = (Color4*)luaL_checkudata(L, 1, RCBN_COLOR4_METATABLE);
    std::string str = "Color4(" + std::to_string(color->r) + ", " + std::to_string(color->g) + ", " + 
                      std::to_string(color->b) + ", " + std::to_string(color->a) + ")";
    lua_pushstring(L, str.c_str());
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