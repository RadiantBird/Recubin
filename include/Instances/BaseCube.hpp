#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Instances/Spatial.hpp>
#include <include/Instances/Workspace.hpp>
#include <include/Math/Vector3.hpp>
#include <include/Math/Quaternion.hpp>
#include <include/Util/Color4.hpp>
#include <include/PhysX/PxPhysicsAPI.h>

class BaseCube : public Spatial {
public:
    bool Anchored = false;
    bool CanCollide = true;

    Color4 Color;

    // キャッシュ：自分がどの Workspace に登録されているか
    Workspace* lastWorkspace = nullptr;

    physx::PxRigidDynamicLockFlags LockFlags = (physx::PxRigidDynamicLockFlags)0;
    physx::PxRigidActor* actor = nullptr;

    BaseCube(Vector3 Pos, Vector3 Sz);
    
    virtual string GetClassName() override;
    virtual bool IsA(std::string name) override;
    void syncPhysics();

    // 自律的な登録・解除ロジック
    void onAncestorChanged() override;
};
