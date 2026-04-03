#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Math/Vector3.hpp>
#include <include/Math/Quaternion.hpp>
#include <include/Util/Color4.hpp>
#include <include/PhysX/PxPhysicsAPI.h> // PhysX Actor保持用

class BaseCube : public Instance {
public:
    bool Anchored = false;
    bool CanCollide = true;

    Vector3 Position;
    Vector3 Size;
    Quaternion Rotation;
    Color4 Color;

    // 物理エンジン側が生成してセットするActor
    physx::PxRigidActor* actor = nullptr;

    BaseCube(Vector3 Pos, Vector3 Sz);
    
    virtual bool IsA(std::string name) override;
    // 物理世界の状態をプロパティに同期する
    void syncPhysics();
};