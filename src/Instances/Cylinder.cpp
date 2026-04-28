#include <Instances/Cylinder.hpp>
#include <GL/glew.h>
#include <cmath>

unsigned int Cylinder::defaultTextureID = 0;
unsigned int Cylinder::s_VAO = 0;
unsigned int Cylinder::s_VBO = 0;
unsigned int Cylinder::s_EBO = 0;
int Cylinder::s_IndexCount = 0;

static const int CYL_SEG = 32;
static const float CYL_PI = 3.14159265358979323846f;

void Cylinder::initGeometry() {
    if (s_VAO != 0) return;

    std::vector<float> vbo;
    std::vector<unsigned int> ebo;

    auto pushVert = [&](float px, float py, float pz,
                        float nx, float ny, float nz,
                        float u,  float v) {
        vbo.push_back(px); vbo.push_back(py); vbo.push_back(pz);
        vbo.push_back(nx); vbo.push_back(ny); vbo.push_back(nz);
        vbo.push_back(u);  vbo.push_back(v);
    };

    // トップキャップ (法線 +Y)
    unsigned int topBase = 0;
    pushVert(0.f, 0.5f, 0.f, 0.f, 1.f, 0.f, 0.5f, 0.5f); // 中心
    for (int i = 0; i < CYL_SEG; ++i) {
        float a = 2.f * CYL_PI * i / CYL_SEG;
        float x = 0.5f * std::cos(a);
        float z = 0.5f * std::sin(a);
        pushVert(x, 0.5f, z, 0.f, 1.f, 0.f, 0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a));
    }
    for (int i = 0; i < CYL_SEG; ++i) {
        ebo.push_back(topBase);
        ebo.push_back(topBase + 1 + i);
        ebo.push_back(topBase + 1 + (i + 1) % CYL_SEG);
    }

    // ボトムキャップ (法線 -Y)
    unsigned int botBase = (unsigned int)vbo.size() / 8;
    pushVert(0.f, -0.5f, 0.f, 0.f, -1.f, 0.f, 0.5f, 0.5f); // 中心
    for (int i = 0; i < CYL_SEG; ++i) {
        float a = 2.f * CYL_PI * i / CYL_SEG;
        float x = 0.5f * std::cos(a);
        float z = 0.5f * std::sin(a);
        pushVert(x, -0.5f, z, 0.f, -1.f, 0.f, 0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a));
    }
    for (int i = 0; i < CYL_SEG; ++i) {
        ebo.push_back(botBase);
        ebo.push_back(botBase + 1 + (i + 1) % CYL_SEG);
        ebo.push_back(botBase + 1 + i);
    }

    // 側面 (法線=径方向)
    for (int i = 0; i < CYL_SEG; ++i) {
        float a0 = 2.f * CYL_PI * i / CYL_SEG;
        float a1 = 2.f * CYL_PI * (i + 1) / CYL_SEG;
        float nx0 = std::cos(a0), nz0 = std::sin(a0);
        float nx1 = std::cos(a1), nz1 = std::sin(a1);
        float u0 = (float)i / CYL_SEG, u1 = (float)(i + 1) / CYL_SEG;

        unsigned int base = (unsigned int)vbo.size() / 8;
        pushVert(0.5f * nx0,  0.5f, 0.5f * nz0, nx0, 0.f, nz0, u0, 1.f);
        pushVert(0.5f * nx0, -0.5f, 0.5f * nz0, nx0, 0.f, nz0, u0, 0.f);
        pushVert(0.5f * nx1, -0.5f, 0.5f * nz1, nx1, 0.f, nz1, u1, 0.f);
        pushVert(0.5f * nx1,  0.5f, 0.5f * nz1, nx1, 0.f, nz1, u1, 1.f);

        ebo.push_back(base + 0); ebo.push_back(base + 1); ebo.push_back(base + 2);
        ebo.push_back(base + 0); ebo.push_back(base + 2); ebo.push_back(base + 3);
    }

    s_IndexCount = (int)ebo.size();

    glGenVertexArrays(1, &s_VAO);
    glGenBuffers(1, &s_VBO);
    glGenBuffers(1, &s_EBO);

    glBindVertexArray(s_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, s_VBO);
    glBufferData(GL_ARRAY_BUFFER, vbo.size() * sizeof(float), vbo.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, ebo.size() * sizeof(unsigned int), ebo.data(), GL_STATIC_DRAW);

    GLsizei stride = 8 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

Cylinder::Cylinder(Vector3 Pos, Vector3 Sz)
    : BaseCube(Pos, Sz)
{
    initGeometry();
}

std::string Cylinder::GetClassName() {
    return "Cylinder";
}

bool Cylinder::IsA(std::string className) {
    if (className == "Cylinder") return true;
    return BaseCube::IsA(className);
}

void Cylinder::draw(int modelLoc, int shaderProgram) {
    glBindVertexArray(s_VAO);

    int colorLoc = glGetUniformLocation(shaderProgram, "ourColor");
    if (colorLoc != -1) {
        glUniform4f(colorLoc, Color.r, Color.g, Color.b, Color.a);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, defaultTextureID ? defaultTextureID : Cylinder::defaultTextureID);
    glDrawElements(GL_TRIANGLES, s_IndexCount, GL_UNSIGNED_INT, nullptr);
}

std::shared_ptr<Instance> Cylinder::clone() const {
    auto copy = std::make_shared<Cylinder>(this->Position, this->Size);
    copy->Name      = this->Name;
    copy->Color     = this->Color;
    copy->Anchored  = this->Anchored;
    copy->CanCollide = this->CanCollide;
    copy->cframe    = this->cframe;
    for (auto const& [name, child] : children) {
        copy->addChild(child->clone());
    }
    return copy;
}

std::vector<physx::PxVec3> Cylinder::getConvexVertices() const {
    std::vector<physx::PxVec3> verts;
    for (int i = 0; i < CYL_SEG; ++i) {
        float a = 2.f * CYL_PI * i / CYL_SEG;
        float x = 0.5f * std::cos(a);
        float z = 0.5f * std::sin(a);
        verts.push_back({ x,  0.5f, z });
        verts.push_back({ x, -0.5f, z });
    }
    return verts;
}
