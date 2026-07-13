#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Instances/Spatial.hpp>
#include <include/Core/RCBNScriptSignal.hpp>
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
    std::shared_ptr<RCBNScriptSignal> Touched;

    bool Anchored = false;
    bool CanCollide = true;
    bool CastShadow = true;
    bool Unlit = false;
    bool UseTriplanar = false;
    float TextureScale = 1.0f;

    Color4 Color;
    Material material = Material::GetDefault(MaterialType::Plastic);
    float MassDensity = 1.0f;   // 質量計算用の密度（Physics::createActorのupdateMassAndInertia基準）

    // キャッシュ：自分がどの Workspace に登録されているか
    Workspace* lastWorkspace = nullptr;

    physx::PxRigidDynamicLockFlags LockFlags = (physx::PxRigidDynamicLockFlags)0;
    physx::PxRigidActor* actor = nullptr;
    physx::PxTransform m_compoundLocalOffset = physx::PxTransform(physx::PxIdentity);
    // このキューブがアンカーを含むWeldのキネマティックcompoundのメンバーであるか。
    // true のときは syncPhysics() でキネマティック駆動を setKinematicTarget ではなく
    // setGlobalPose(即時) で行い、アニメ駆動部(Head等)への追従ラグを無くす。
    // 単独のキネマティック(動くプラットフォーム等)は false のままで通常通り setKinematicTarget を使う。
    bool m_weldKinematic = false;

    BaseCube(Vector3 Pos, Vector3 Sz);
    virtual ~BaseCube();

    virtual PhysicsShape getPhysicsShape() const { return PhysicsShape::Box; }
    virtual std::vector<physx::PxVec3> getConvexVertices() const { return {}; }

    // ハイライト描画(塗り+輪郭)用にジオメトリのVAO/インデックス数を返す。
    // 描画可能なジオメトリがなければ0を返す(MeshCube未ロード時など)
    virtual unsigned int getHighlightVAO() const { return 0; }
    virtual unsigned int getHighlightIndexCount() const { return 0; }

    // ハイライト輪郭線(リボン描画)用の、ローカル空間の「硬いエッジ」端点列。
    // N本のエッジ → N*6 floats (x0,y0,z0,x1,y1,z1の繰り返し)。空なら輪郭なし。
    virtual const std::vector<float>& getHighlightEdgeVerts() const {
        static const std::vector<float> empty;
        return empty;
    }

    // 子デカールから指定方向のテクスチャIDを取得するヘルパー
    // 該当するデカールがなければ fallback を返す
    unsigned int getDecalTexture(Face face, unsigned int fallback) const;
    
    virtual bool IsA(std::string name) override;
    void syncPhysics();
    void teleportTo(Vector3 pos);
    void setSize(Vector3 newSize);
    void setRotation(Quaternion rot);
    void setAnchored(bool anchored);
    void setMaterial(const Material& m);
    void setMassDensity(float d);

    // 自律的な登録・解除ロジック
    void onAncestorChanged() override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
};
