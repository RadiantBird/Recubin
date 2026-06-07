#include <Instances/TriangularPrism.hpp>
#include <GL/glew.h>

unsigned int TriangularPrism::defaultTextureID = 0;
unsigned int TriangularPrism::s_VAO = 0;
unsigned int TriangularPrism::s_VBO = 0;
unsigned int TriangularPrism::s_EBO = 0;
int TriangularPrism::s_IndexCount = 0;

// 正三角形を半径0.5の円に内接 (角度: 90°, 210°, 330°)
// A=(0.5, y, 0), B=(-0.25, y, 0.4330), C=(-0.25, y, -0.4330)
static const float TP_AX =  0.5f,   TP_AZ =  0.0f;
static const float TP_BX = -0.25f,  TP_BZ =  0.4330f;
static const float TP_CX = -0.25f,  TP_CZ = -0.4330f;

void TriangularPrism::initGeometry() {
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

    // トップ三角 (法線 +Y)
    {
        unsigned int base = (unsigned int)vbo.size() / 8;
        pushVert(TP_AX, 0.5f, TP_AZ,  0.f, 1.f, 0.f,  0.f, 0.f);
        pushVert(TP_BX, 0.5f, TP_BZ,  0.f, 1.f, 0.f,  1.f, 0.f);
        pushVert(TP_CX, 0.5f, TP_CZ,  0.f, 1.f, 0.f,  0.5f, 1.f);
        ebo.push_back(base); ebo.push_back(base+1); ebo.push_back(base+2);
    }

    // ボトム三角 (法線 -Y, 逆巻き)
    {
        unsigned int base = (unsigned int)vbo.size() / 8;
        pushVert(TP_AX, -0.5f, TP_AZ,  0.f, -1.f, 0.f,  0.f, 0.f);
        pushVert(TP_CX, -0.5f, TP_CZ,  0.f, -1.f, 0.f,  1.f, 0.f);
        pushVert(TP_BX, -0.5f, TP_BZ,  0.f, -1.f, 0.f,  0.5f, 1.f);
        ebo.push_back(base); ebo.push_back(base+1); ebo.push_back(base+2);
    }

    // 矩形面の追加ヘルパー (4頂点 + 2三角)
    auto addQuad = [&](float p0x, float p0y, float p0z,
                       float p1x, float p1y, float p1z,
                       float p2x, float p2y, float p2z,
                       float p3x, float p3y, float p3z,
                       float nx, float ny, float nz) {
        unsigned int base = (unsigned int)vbo.size() / 8;
        pushVert(p0x, p0y, p0z, nx, ny, nz, 0.f, 1.f);
        pushVert(p1x, p1y, p1z, nx, ny, nz, 0.f, 0.f);
        pushVert(p2x, p2y, p2z, nx, ny, nz, 1.f, 0.f);
        pushVert(p3x, p3y, p3z, nx, ny, nz, 1.f, 1.f);
        ebo.push_back(base); ebo.push_back(base+1); ebo.push_back(base+2);
        ebo.push_back(base); ebo.push_back(base+2); ebo.push_back(base+3);
    };

    // A-B 面の法線 (AB辺に垂直な外向きベクトル)
    {
        float ex = TP_BX - TP_AX, ez = TP_BZ - TP_AZ;
        float len = std::sqrt(ex*ex + ez*ez);
        float nx = ez / len, nz = -ex / len; // 外向きに回転
        addQuad(TP_AX, 0.5f, TP_AZ,  TP_AX, -0.5f, TP_AZ,
                TP_BX, -0.5f, TP_BZ, TP_BX,  0.5f, TP_BZ,
                nx, 0.f, nz);
    }

    // B-C 面
    {
        float ex = TP_CX - TP_BX, ez = TP_CZ - TP_BZ;
        float len = std::sqrt(ex*ex + ez*ez);
        float nx = ez / len, nz = -ex / len;
        addQuad(TP_BX, 0.5f, TP_BZ,  TP_BX, -0.5f, TP_BZ,
                TP_CX, -0.5f, TP_CZ, TP_CX,  0.5f, TP_CZ,
                nx, 0.f, nz);
    }

    // C-A 面
    {
        float ex = TP_AX - TP_CX, ez = TP_AZ - TP_CZ;
        float len = std::sqrt(ex*ex + ez*ez);
        float nx = ez / len, nz = -ex / len;
        addQuad(TP_CX, 0.5f, TP_CZ,  TP_CX, -0.5f, TP_CZ,
                TP_AX, -0.5f, TP_AZ, TP_AX,  0.5f, TP_AZ,
                nx, 0.f, nz);
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

TriangularPrism::TriangularPrism(Vector3 Pos, Vector3 Sz)
    : BaseCube(Pos, Sz)
{
    initGeometry();
}

std::string TriangularPrism::getClassName() {
    return "TriangularPrism";
}

bool TriangularPrism::IsA(std::string className) {
    if (className == "TriangularPrism") return true;
    return BaseCube::IsA(className);
}

// インデックスバッファのリージョン境界 (initGeometry の生成順)
// [0..3)   : Top   (Face::Top)
// [3..6)   : Bottom(Face::Bottom)
// [6..12)  : 面AB  (Face::Front)
// [12..18) : 面BC  (Face::Back)
// [18..24) : 面CA  (Face::Right)
static const int TP_TOP_OFF  = 0,  TP_TOP_COUNT  = 3;
static const int TP_BOT_OFF  = 3,  TP_BOT_COUNT  = 3;
static const int TP_AB_OFF   = 6,  TP_AB_COUNT   = 6;
static const int TP_BC_OFF   = 12, TP_BC_COUNT   = 6;
static const int TP_CA_OFF   = 18, TP_CA_COUNT   = 6;

void TriangularPrism::draw(int modelLoc, int shaderProgram) {
    glBindVertexArray(s_VAO);

    int colorLoc = glGetUniformLocation(shaderProgram, "ourColor");
    if (colorLoc != -1) {
        glUniform4f(colorLoc, Color.r, Color.g, Color.b, Color.a);
    }

    glActiveTexture(GL_TEXTURE0);

    unsigned int topTex   = getDecalTexture(Face::Top,    defaultTextureID);
    unsigned int botTex   = getDecalTexture(Face::Bottom, defaultTextureID);
    unsigned int frontTex = getDecalTexture(Face::Front,  defaultTextureID);
    unsigned int backTex  = getDecalTexture(Face::Back,   defaultTextureID);
    unsigned int rightTex = getDecalTexture(Face::Right,  defaultTextureID);

    auto draw = [](int count, int off) {
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT,
                       (void*)(uintptr_t)(off * sizeof(unsigned int)));
    };

    glBindTexture(GL_TEXTURE_2D, topTex);   draw(TP_TOP_COUNT, TP_TOP_OFF);
    glBindTexture(GL_TEXTURE_2D, botTex);   draw(TP_BOT_COUNT, TP_BOT_OFF);
    glBindTexture(GL_TEXTURE_2D, frontTex); draw(TP_AB_COUNT,  TP_AB_OFF);
    glBindTexture(GL_TEXTURE_2D, backTex);  draw(TP_BC_COUNT,  TP_BC_OFF);
    glBindTexture(GL_TEXTURE_2D, rightTex); draw(TP_CA_COUNT,  TP_CA_OFF);
}

std::shared_ptr<Instance> TriangularPrism::clone() const {
    auto copy = std::make_shared<TriangularPrism>(this->Position, this->Size);
    copy->Name       = this->Name;
    copy->Color      = this->Color;
    copy->Anchored   = this->Anchored;
    copy->CanCollide = this->CanCollide;
    copy->cframe     = this->cframe;
    for (auto const& [name, child] : children) {
        copy->addChild(child->clone());
    }
    return copy;
}

std::vector<physx::PxVec3> TriangularPrism::getConvexVertices() const {
    return {
        { TP_AX,  0.5f, TP_AZ },
        { TP_BX,  0.5f, TP_BZ },
        { TP_CX,  0.5f, TP_CZ },
        { TP_AX, -0.5f, TP_AZ },
        { TP_BX, -0.5f, TP_BZ },
        { TP_CX, -0.5f, TP_CZ },
    };
}
