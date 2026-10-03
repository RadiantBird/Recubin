#include <Instances/Wedge.hpp>
#include <Core/Renderer.hpp>
#include <Util/MeshEdges.hpp>
#include <Util/GLUniformCache.hpp>
#include <GL/glew.h>
#include <Core/PropertyRegistry.hpp>
#include <cmath>

static const bool s_wedgeRegistered = [] {
    PropertyRegistry::registerClass("Wedge", "BaseCube", {});
    return true;
}();

unsigned int Wedge::defaultTextureID = 0;
unsigned int Wedge::s_VAO = 0;
unsigned int Wedge::s_VBO = 0;
unsigned int Wedge::s_EBO = 0;
int Wedge::s_IndexCount = 0;
std::vector<float> Wedge::s_HighlightEdgeVerts;

// 断面(YZ)の直角三角形。P0=前方下端, P1=背面下端, P2=背面上端（直角はP1）
static const float WG_Y0 = -0.5f, WG_Z0 = -0.5f;
static const float WG_Y1 = -0.5f, WG_Z1 =  0.5f;
static const float WG_Y2 =  0.5f, WG_Z2 =  0.5f;
static const float WG_XL = -0.5f, WG_XR = 0.5f;

// インデックスバッファのリージョン境界 (initGeometry の生成順)
// [0..3)   : 左側面  (Face::Left)
// [3..6)   : 右側面  (Face::Right)
// [6..12)  : 底面    (Face::Bottom)
// [12..18) : 背面    (Face::Back)
// [18..24) : 斜面    (Face::Top)  ※前方向きの垂直面は無いため斜面にTopを割り当てる
static const int WG_LEFT_OFF  = 0,  WG_LEFT_COUNT  = 3;
static const int WG_RIGHT_OFF = 3,  WG_RIGHT_COUNT = 3;
static const int WG_BOT_OFF   = 6,  WG_BOT_COUNT   = 6;
static const int WG_BACK_OFF  = 12, WG_BACK_COUNT  = 6;
static const int WG_SLOPE_OFF = 18, WG_SLOPE_COUNT = 6;

void Wedge::initGeometry() {
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

    struct P { float x, y, z; };

    // 外向き法線(n)に対して反時計回りになるよう、必要なら巻き順を反転して三角形を追加する
    auto addTriangle = [&](P a, P b, P c, P n, float ua, float va, float ub, float vb, float uc, float vc) {
        const float ex1 = b.x - a.x, ey1 = b.y - a.y, ez1 = b.z - a.z;
        const float ex2 = c.x - a.x, ey2 = c.y - a.y, ez2 = c.z - a.z;
        const float cx = ey1 * ez2 - ez1 * ey2;
        const float cy = ez1 * ex2 - ex1 * ez2;
        const float cz = ex1 * ey2 - ey1 * ex2;
        const bool flip = (cx * n.x + cy * n.y + cz * n.z) < 0.0f;
        const unsigned int base = (unsigned int)vbo.size() / 8;
        pushVert(a.x, a.y, a.z, n.x, n.y, n.z, ua, va);
        pushVert(b.x, b.y, b.z, n.x, n.y, n.z, ub, vb);
        pushVert(c.x, c.y, c.z, n.x, n.y, n.z, uc, vc);
        if (flip) { ebo.push_back(base); ebo.push_back(base + 2); ebo.push_back(base + 1); }
        else      { ebo.push_back(base); ebo.push_back(base + 1); ebo.push_back(base + 2); }
    };

    // 四角形 (p0,p1,p2,p3 は周回順)
    auto addQuad = [&](P p0, P p1, P p2, P p3, P n) {
        addTriangle(p0, p1, p2, n, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f);
        addTriangle(p0, p2, p3, n, 0.f, 1.f, 1.f, 0.f, 1.f, 1.f);
    };

    // 左(-X) / 右(+X) の三角形
    addTriangle({WG_XL, WG_Y0, WG_Z0}, {WG_XL, WG_Y1, WG_Z1}, {WG_XL, WG_Y2, WG_Z2},
                {-1.f, 0.f, 0.f}, 0.f, 0.f, 1.f, 0.f, 1.f, 1.f);
    addTriangle({WG_XR, WG_Y0, WG_Z0}, {WG_XR, WG_Y1, WG_Z1}, {WG_XR, WG_Y2, WG_Z2},
                {1.f, 0.f, 0.f}, 0.f, 0.f, 1.f, 0.f, 1.f, 1.f);

    // 底面 (-Y)
    addQuad({WG_XL, WG_Y0, WG_Z0}, {WG_XR, WG_Y0, WG_Z0},
            {WG_XR, WG_Y1, WG_Z1}, {WG_XL, WG_Y1, WG_Z1}, {0.f, -1.f, 0.f});

    // 背面 (+Z)
    addQuad({WG_XL, WG_Y1, WG_Z1}, {WG_XR, WG_Y1, WG_Z1},
            {WG_XR, WG_Y2, WG_Z2}, {WG_XL, WG_Y2, WG_Z2}, {0.f, 0.f, 1.f});

    // 斜面（上かつ前方向きの法線 (0,1,-1)/√2）
    {
        const float inv = 1.0f / std::sqrt(2.0f);
        addQuad({WG_XL, WG_Y0, WG_Z0}, {WG_XR, WG_Y0, WG_Z0},
                {WG_XR, WG_Y2, WG_Z2}, {WG_XL, WG_Y2, WG_Z2}, {0.f, inv, -inv});
    }

    s_IndexCount = (int)ebo.size();

    s_HighlightEdgeVerts = MeshEdges::extractHardEdges(vbo.data(), vbo.size() / 8, 8, 0, ebo.data(), ebo.size(), 20.0f);

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

Wedge::Wedge(Vector3 Pos, Vector3 Sz)
    : Named<Wedge, BaseCube>(Pos, Sz)
{
    if (Renderer::instance) initGeometry();
}

void Wedge::draw(int modelLoc, int shaderProgram) {
    glBindVertexArray(s_VAO);

    static CachedUniform s_colorLocCache;
    int colorLoc = cachedUniformLocation(shaderProgram, s_colorLocCache, "ourColor");
    if (colorLoc != -1) {
        glUniform4f(colorLoc, Color.r, Color.g, Color.b, Color.a);
    }

    glActiveTexture(GL_TEXTURE0);

    auto drawRegion = [&](Face face, int count, int off) {
        glBindTexture(GL_TEXTURE_2D, getDecalTexture(face, defaultTextureID));
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT,
                       (void*)(uintptr_t)(off * sizeof(unsigned int)));
    };

    drawRegion(Face::Left,   WG_LEFT_COUNT,  WG_LEFT_OFF);
    drawRegion(Face::Right,  WG_RIGHT_COUNT, WG_RIGHT_OFF);
    drawRegion(Face::Bottom, WG_BOT_COUNT,   WG_BOT_OFF);
    drawRegion(Face::Back,   WG_BACK_COUNT,  WG_BACK_OFF);
    drawRegion(Face::Top,    WG_SLOPE_COUNT, WG_SLOPE_OFF);
}

std::shared_ptr<Instance> Wedge::clone() const {
    auto copy = std::make_shared<Wedge>(this->getPosition(), this->Size);
    PropertyRegistry::cloneFields(this, copy.get(), "Wedge");
    cloneBaseCubeStateAndChildrenTo(copy);
    return copy;
}

std::vector<Vector3> Wedge::getConvexVertices() const {
    return {
        { WG_XL, WG_Y0, WG_Z0 }, { WG_XR, WG_Y0, WG_Z0 },
        { WG_XL, WG_Y1, WG_Z1 }, { WG_XR, WG_Y1, WG_Z1 },
        { WG_XL, WG_Y2, WG_Z2 }, { WG_XR, WG_Y2, WG_Z2 },
    };
}
