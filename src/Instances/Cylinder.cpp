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
    for (int i = 0; i <= CYL_SEG; ++i) {
        float a = 2.f * CYL_PI * i / CYL_SEG - CYL_PI / 4.f;
        float x = 0.5f * std::cos(a);
        float z = 0.5f * std::sin(a);
        pushVert(x, 0.5f, z, 0.f, 1.f, 0.f, 0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a));
    }
    for (int i = 0; i < CYL_SEG; ++i) {
        ebo.push_back(topBase);
        ebo.push_back(topBase + 1 + i);
        ebo.push_back(topBase + 2 + i);
    }

    // ボトムキャップ (法線 -Y)
    unsigned int botBase = (unsigned int)vbo.size() / 8;
    pushVert(0.f, -0.5f, 0.f, 0.f, -1.f, 0.f, 0.5f, 0.5f); // 中心
    for (int i = 0; i <= CYL_SEG; ++i) {
        float a = 2.f * CYL_PI * i / CYL_SEG - CYL_PI / 4.f;
        float x = 0.5f * std::cos(a);
        float z = 0.5f * std::sin(a);
        pushVert(x, -0.5f, z, 0.f, -1.f, 0.f, 0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a));
    }
    for (int i = 0; i < CYL_SEG; ++i) {
        ebo.push_back(botBase);
        ebo.push_back(botBase + 2 + i);
        ebo.push_back(botBase + 1 + i);
    }

    // 側面 (4つのクアドラントに分割)
    for (int q = 0; q < 4; ++q) {
        for (int i = 0; i < CYL_SEG / 4; ++i) {
            int absI = q * (CYL_SEG / 4) + i;
            float a0 = 2.f * CYL_PI * absI / CYL_SEG - CYL_PI / 4.f;
            float a1 = 2.f * CYL_PI * (absI + 1) / CYL_SEG - CYL_PI / 4.f;
            float nx0 = std::cos(a0), nz0 = std::sin(a0);
            float nx1 = std::cos(a1), nz1 = std::sin(a1);
            float u0 = (float)i / (CYL_SEG / 4);
            float u1 = (float)(i + 1) / (CYL_SEG / 4);

            unsigned int base = (unsigned int)vbo.size() / 8;
            pushVert(0.5f * nx0,  0.5f, 0.5f * nz0, nx0, 0.f, nz0, u0, 1.f);
            pushVert(0.5f * nx0, -0.5f, 0.5f * nz0, nx0, 0.f, nz0, u0, 0.f);
            pushVert(0.5f * nx1, -0.5f, 0.5f * nz1, nx1, 0.f, nz1, u1, 0.f);
            pushVert(0.5f * nx1,  0.5f, 0.5f * nz1, nx1, 0.f, nz1, u1, 1.f);

            ebo.push_back(base + 0); ebo.push_back(base + 1); ebo.push_back(base + 2);
            ebo.push_back(base + 0); ebo.push_back(base + 2); ebo.push_back(base + 3);
        }
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

// インデックスバッファのリージョン境界
static const int CYL_TOP_COUNT  = CYL_SEG * 3;          // 96
static const int CYL_BOT_COUNT  = CYL_SEG * 3;          // 96
static const int CYL_QUAD_COUNT = (CYL_SEG / 4) * 6;    // 48 (8 segments * 6 indices)
static const int CYL_TOP_OFF    = 0;
static const int CYL_BOT_OFF    = CYL_TOP_COUNT;        // 96
static const int CYL_SIDE_OFF_0 = CYL_TOP_COUNT + CYL_BOT_COUNT; // 192 (Right)
static const int CYL_SIDE_OFF_1 = CYL_SIDE_OFF_0 + CYL_QUAD_COUNT; // 240 (Back)
static const int CYL_SIDE_OFF_2 = CYL_SIDE_OFF_1 + CYL_QUAD_COUNT; // 288 (Left)
static const int CYL_SIDE_OFF_3 = CYL_SIDE_OFF_2 + CYL_QUAD_COUNT; // 336 (Front)

void Cylinder::draw(int modelLoc, int shaderProgram) {
    glBindVertexArray(s_VAO);

    int colorLoc = glGetUniformLocation(shaderProgram, "ourColor");
    if (colorLoc != -1) {
        glUniform4f(colorLoc, Color.r, Color.g, Color.b, Color.a);
    }

    glActiveTexture(GL_TEXTURE0);

    // Top cap
    glBindTexture(GL_TEXTURE_2D, getDecalTexture(Face::Top, defaultTextureID));
    glDrawElements(GL_TRIANGLES, CYL_TOP_COUNT, GL_UNSIGNED_INT, (void*)(uintptr_t)(CYL_TOP_OFF * sizeof(unsigned int)));

    // Bottom cap
    glBindTexture(GL_TEXTURE_2D, getDecalTexture(Face::Bottom, defaultTextureID));
    glDrawElements(GL_TRIANGLES, CYL_BOT_COUNT, GL_UNSIGNED_INT, (void*)(uintptr_t)(CYL_BOT_OFF * sizeof(unsigned int)));

    // Sides: Right(0), Back(1), Left(2), Front(3)
    glBindTexture(GL_TEXTURE_2D, getDecalTexture(Face::Right, defaultTextureID));
    glDrawElements(GL_TRIANGLES, CYL_QUAD_COUNT, GL_UNSIGNED_INT, (void*)(uintptr_t)(CYL_SIDE_OFF_0 * sizeof(unsigned int)));

    glBindTexture(GL_TEXTURE_2D, getDecalTexture(Face::Back, defaultTextureID));
    glDrawElements(GL_TRIANGLES, CYL_QUAD_COUNT, GL_UNSIGNED_INT, (void*)(uintptr_t)(CYL_SIDE_OFF_1 * sizeof(unsigned int)));

    glBindTexture(GL_TEXTURE_2D, getDecalTexture(Face::Left, defaultTextureID));
    glDrawElements(GL_TRIANGLES, CYL_QUAD_COUNT, GL_UNSIGNED_INT, (void*)(uintptr_t)(CYL_SIDE_OFF_2 * sizeof(unsigned int)));

    glBindTexture(GL_TEXTURE_2D, getDecalTexture(Face::Front, defaultTextureID));
    glDrawElements(GL_TRIANGLES, CYL_QUAD_COUNT, GL_UNSIGNED_INT, (void*)(uintptr_t)(CYL_SIDE_OFF_3 * sizeof(unsigned int)));
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
