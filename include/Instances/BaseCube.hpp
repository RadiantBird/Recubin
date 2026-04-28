#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Instances/Spatial.hpp>
#include <include/Instances/Workspace.hpp>
#include <include/Math/Vector3.hpp>
#include <include/Math/Quaternion.hpp>
#include <include/Util/Color4.hpp>
#include <include/Util/Material.hpp>
#include <include/PhysX/PxPhysicsAPI.h>
#include <include/Instances/Decal.hpp>
#include <vector>

enum class PhysicsShape { Box, Sphere, ConvexMesh };

class BaseCube : public Spatial {
public:
    bool Anchored = false;
    bool CanCollide = true;

    Color4 Color;
    Material material = Material::GetDefault(MaterialType::Plastic);

    // キャッシュ：自分がどの Workspace に登録されているか
    Workspace* lastWorkspace = nullptr;

    physx::PxRigidDynamicLockFlags LockFlags = (physx::PxRigidDynamicLockFlags)0;
    physx::PxRigidActor* actor = nullptr;

    BaseCube(Vector3 Pos, Vector3 Sz);
    virtual ~BaseCube();

    virtual PhysicsShape getPhysicsShape() const { return PhysicsShape::Box; }
    virtual std::vector<physx::PxVec3> getConvexVertices() const { return {}; }

    // 子デカールから指定方向のテクスチャIDを取得するヘルパー
    // 該当するデカールがなければ fallback を返す
    unsigned int getDecalTexture(Face face, unsigned int fallback) const;
    
    virtual string GetClassName() override;
    virtual bool IsA(std::string name) override;
    void syncPhysics();
    void teleportTo(Vector3 pos);
    void setSize(Vector3 newSize);
    void setRotation(Quaternion rot);

    // 自律的な登録・解除ロジック
    void onAncestorChanged() override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
};
