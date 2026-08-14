#pragma once

#include <include/Instances/BaseCube.hpp>
#include <include/Instances/Named.hpp>
#include <vector>

class TriangularPrism : public Named<TriangularPrism, BaseCube> {
public:
    static constexpr const char* ClassName = "TriangularPrism";

    static unsigned int defaultTextureID;
    static unsigned int s_VAO;
    static unsigned int s_VBO;
    static unsigned int s_EBO;
    static int s_IndexCount;
    static std::vector<float> s_HighlightEdgeVerts;

    TriangularPrism(Vector3 Pos, Vector3 Sz);

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
