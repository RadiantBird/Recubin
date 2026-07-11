#pragma once

#include <include/Instances/BaseCube.hpp>
#include <include/Instances/Named.hpp>
#include <include/Math/Vector3.hpp>
#include <include/Math/Vector2.hpp>
#include <Util/Color4.hpp>
#include <vector>
#include <string>

// MeshCube描画用: UV空間の中心座標+半径でブレンドするDecal記述子
struct UVDecalDesc {
    unsigned int textureID;
    Vector2      center;
    float        radius;
    Color4       color;
};

// MeshCube::raycastLocal() の結果。ヒットした三角形のUVをバリセントリック補間した値を持つ
struct MeshHitResult {
    bool    hit = false;
    float   t   = 0.0f;
    Vector2 uv;
};

// GLBメッシュの頂点。単位空間(最大軸が1.0になるよう正規化・中心化済み)で保持する。
struct MeshVertex {
    Vector3 Position;
    Vector3 Normal;
    float U, V;
    float MatAlpha = 1.0f; // マテリアルのbaseColorFactor.a (透過素材対応)
};

class MeshCube : public Named<MeshCube, BaseCube> {
public:
    static constexpr const char* ClassName = "MeshCube";

    MeshCube(Vector3 Pos, Vector3 Sz);
    virtual ~MeshCube();

    std::string MeshFile; // GLBファイルパス(空文字なら未ロード)

    void draw(int modelLoc, int shaderProgram);

    virtual bool IsA(std::string name) override;
    virtual void setProperty(const std::string& name, const YAML::Node& value) override;
    std::shared_ptr<Instance> clone() const override;

    PhysicsShape getPhysicsShape() const override { return PhysicsShape::ConvexMesh; }
    std::vector<physx::PxVec3> getConvexVertices() const override;

    // GLBファイルをロードしてメッシュ・テクスチャ・GPUバッファを再構築する。
    // 失敗時は false を返し、既存のジオメトリ・MeshFile は変更しない。
    bool loadFromGLB(const std::string& path);

    bool hasGeometry() const { return m_VAO != 0; }
    unsigned int getVAO() const { return m_VAO; }
    unsigned int getIndexCount() const { return m_indexCount; }

    // 全MeshVertexのU/Vそれぞれのmin/maxを走査し、範囲(max-min)が閾値未満なら
    // 縮退(UV未設定/重複)と判定してfalseを返す。
    bool hasValidUV() const;

    // xatlasでUVを再生成する。成功時はm_cpuVertices/m_cpuIndicesを新しい配列に
    // 差し替えてtrueを返す。失敗時は既存ジオメトリを一切変更せずfalseを返す。
    // GPUアップロード(uploadToGPU)はこの関数の責務外で、呼び出し元が行う。
    bool regenerateUV();

    // regenerateUV()等でCPU側のジオメトリを差し替えた後、GPUバッファへ反映する。
    void uploadToGPU();

    // childrenをIsA("Decal")で走査し、先着順で最大maxCount個のUV空間Decal記述子を返す。
    // MeshCubeにはFace概念が無いため、Face一致判定は行わずすべてのDecal子を対象にする。
    std::vector<UVDecalDesc> collectUVDecals(int maxCount = 8) const;

    // MeshCubeのローカル空間(loadFromGLB時に最大軸1.0で正規化・中心化済み)でのレイ原点・方向を
    // 受け取り、m_cpuVertices/m_cpuIndicesの三角形をMöller–Trumbore法で走査してレイに最も近い
    // (tが最小の)交点のUVを返す。ローカル→ワールド変換は呼び出し側の責務。PhysXには触れない。
    MeshHitResult raycastLocal(const Vector3& localOri, const Vector3& localDir) const;

private:
    unsigned int m_VAO = 0;
    unsigned int m_VBO = 0;
    unsigned int m_EBO = 0;
    unsigned int m_indexCount = 0;
    unsigned int m_textureID = 0;

    std::vector<MeshVertex>   m_cpuVertices;
    std::vector<unsigned int> m_cpuIndices;

    void releaseMeshBuffers();
    void releaseGPU();
};
