#pragma once

#include <include/Instances/BaseCube.hpp>
#include <include/Instances/Named.hpp>
#include <include/Math/Vector3.hpp>
#include <vector>
#include <string>

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

private:
    unsigned int m_VAO = 0;
    unsigned int m_VBO = 0;
    unsigned int m_EBO = 0;
    unsigned int m_indexCount = 0;
    unsigned int m_textureID = 0;

    std::vector<MeshVertex>   m_cpuVertices;
    std::vector<unsigned int> m_cpuIndices;

    void uploadToGPU();
    void releaseGPU();
};
