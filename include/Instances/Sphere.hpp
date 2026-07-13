#pragma once

#include <include/Instances/BaseCube.hpp>
#include <include/Instances/Named.hpp>
#include <vector>

class Sphere : public Named<Sphere, BaseCube> {
public:
    static constexpr const char* ClassName = "Sphere";

    static unsigned int defaultTextureID;
    static unsigned int s_VAO;
    static unsigned int s_VBO;
    static unsigned int s_EBO;
    static int s_IndexCount;
    static int s_FaceIndexCount;
    static std::vector<float> s_HighlightEdgeVerts;

    Sphere(Vector3 Pos, Vector3 Sz);

    void draw(int modelLoc, int shaderProgram);

    virtual bool IsA(std::string name) override;
    std::shared_ptr<Instance> clone() const override;

    PhysicsShape getPhysicsShape() const override { return PhysicsShape::Sphere; }

    unsigned int getHighlightVAO() const override { return s_VAO; }
    unsigned int getHighlightIndexCount() const override { return (unsigned int)s_IndexCount; }
    const std::vector<float>& getHighlightEdgeVerts() const override { return s_HighlightEdgeVerts; }

private:
    static void initGeometry();
};
