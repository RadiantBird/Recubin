#pragma once

#include <include/Instances/BaseCube.hpp>
#include <include/Instances/Named.hpp>
#include <vector>

// 直角三角形の角柱（斜面つきブロック）。
// ローカル座標: YZ断面が直角三角形で X 方向に押し出す。
// 底面(-Y)と背面(+Z)が正方形、斜面は前方(-Z)の下端から背面(+Z)の上端へ上る。
class Wedge : public Named<Wedge, BaseCube> {
public:
    static constexpr const char* ClassName = "Wedge";

    static unsigned int defaultTextureID;
    static unsigned int s_VAO;
    static unsigned int s_VBO;
    static unsigned int s_EBO;
    static int s_IndexCount;
    static std::vector<float> s_HighlightEdgeVerts;

    Wedge(Vector3 Pos, Vector3 Sz);

    void draw(int modelLoc, int shaderProgram);

    std::shared_ptr<Instance> clone() const override;

    PhysicsShape getPhysicsShape() const override { return PhysicsShape::ConvexMesh; }
    std::vector<Vector3> getConvexVertices() const override;

    unsigned int getHighlightVAO() const override { return s_VAO; }
    unsigned int getHighlightIndexCount() const override { return (unsigned int)s_IndexCount; }
    const std::vector<float>& getHighlightEdgeVerts() const override { return s_HighlightEdgeVerts; }

private:
    static void initGeometry();
};
