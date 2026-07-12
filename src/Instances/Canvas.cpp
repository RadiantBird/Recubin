#include <Instances/Canvas.hpp>
#include <Instances/BaseCube.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <GL/glew.h>
#include <algorithm>
#include <cmath>

static const bool s_canvasRegistered = []{
    using namespace PropertyRegistry;
    registerClass("Canvas", "Instance", {
        enumProp<&Canvas::face>("Face",
            {{"Front",0},{"Back",1},{"Top",2},{"Bottom",3},{"Right",4},{"Left",5}},
            /*yamlAsString*/true),
        field<&Canvas::Width>       ("Width",  1, 1024, 1).clampLua(),
        field<&Canvas::Height>      ("Height", 1, 1024, 1).clampLua(),
        field<&Canvas::BackgroundColor>("BackgroundColor"),
    });
    return true;
}();

Canvas::Canvas() : Named<Canvas, Instance>("Canvas") {}

Canvas::~Canvas() {
    if (m_texID) glDeleteTextures(1, &m_texID);
}

bool Canvas::IsA(std::string name) {
    if (name == "Canvas") return true;
    return Instance::IsA(name);
}

void Canvas::setProperty(const std::string& name, const YAML::Node& val) {
    if (PropertyRegistry::loadProperty(this, "Canvas", name, val)) return;
    Instance::setProperty(name, val);
}

std::shared_ptr<Instance> Canvas::clone() const {
    auto copy = std::make_shared<Canvas>();
    copy->Name = Name;
    PropertyRegistry::cloneFields(this, copy.get(), "Canvas");
    copy->m_pixels = m_pixels;
    copy->m_bufferBg[0] = m_bufferBg[0]; copy->m_bufferBg[1] = m_bufferBg[1];
    copy->m_bufferBg[2] = m_bufferBg[2]; copy->m_bufferBg[3] = m_bufferBg[3];
    copy->m_dirty = true;
    for (auto const& [n, child] : children)
        copy->addChild(child->clone());
    return copy;
}

static inline unsigned char colorChannelToByte(float c) {
    int v = (int)std::lround(c * 255.0f);
    return (unsigned char)std::clamp(v, 0, 255);
}

void Canvas::ensureBuffer() {
    Width  = std::clamp(Width, 1, 1024);
    Height = std::clamp(Height, 1, 1024);

    unsigned char bg[4] = {
        colorChannelToByte(BackgroundColor.r),
        colorChannelToByte(BackgroundColor.g),
        colorChannelToByte(BackgroundColor.b),
        colorChannelToByte(BackgroundColor.a),
    };

    if (m_pixels.size() != size_t(Width) * size_t(Height) * 4) {
        m_pixels.assign(size_t(Width) * size_t(Height) * 4, 0);
        for (size_t i = 0; i < m_pixels.size(); i += 4) {
            m_pixels[i + 0] = bg[0];
            m_pixels[i + 1] = bg[1];
            m_pixels[i + 2] = bg[2];
            m_pixels[i + 3] = bg[3];
        }
        m_bufferBg[0] = bg[0]; m_bufferBg[1] = bg[1];
        m_bufferBg[2] = bg[2]; m_bufferBg[3] = bg[3];
        m_dirty = true;
    } else if (bg[0] != m_bufferBg[0] || bg[1] != m_bufferBg[1]
            || bg[2] != m_bufferBg[2] || bg[3] != m_bufferBg[3]) {
        // BackgroundColorが変更された: 旧背景色のままのピクセルだけ塗り替える
        // （SetPixelで描かれた内容は保持する）
        for (size_t i = 0; i < m_pixels.size(); i += 4) {
            if (m_pixels[i + 0] == m_bufferBg[0] && m_pixels[i + 1] == m_bufferBg[1]
             && m_pixels[i + 2] == m_bufferBg[2] && m_pixels[i + 3] == m_bufferBg[3]) {
                m_pixels[i + 0] = bg[0];
                m_pixels[i + 1] = bg[1];
                m_pixels[i + 2] = bg[2];
                m_pixels[i + 3] = bg[3];
            }
        }
        m_bufferBg[0] = bg[0]; m_bufferBg[1] = bg[1];
        m_bufferBg[2] = bg[2]; m_bufferBg[3] = bg[3];
        m_dirty = true;
    }
}

void Canvas::ensureGPU() {
    ensureBuffer();

    if (m_texID == 0 || m_texW != Width || m_texH != Height) {
        if (m_texID) glDeleteTextures(1, &m_texID);
        glGenTextures(1, &m_texID);
        glBindTexture(GL_TEXTURE_2D, m_texID);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, Width, Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_texW = Width;
        m_texH = Height;
        m_dirty = true;
    }

    if (m_dirty) {
        glBindTexture(GL_TEXTURE_2D, m_texID);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, Width, Height, GL_RGBA, GL_UNSIGNED_BYTE, m_pixels.data());
        m_dirty = false;
    }
}

bool Canvas::setPixel(int x, int y, const Color4& c) {
    ensureBuffer();
    if (x < 0 || y < 0 || x >= Width || y >= Height) return false;
    int row = Height - 1 - y;
    size_t idx = (size_t(row) * size_t(Width) + size_t(x)) * 4;
    m_pixels[idx + 0] = colorChannelToByte(c.r);
    m_pixels[idx + 1] = colorChannelToByte(c.g);
    m_pixels[idx + 2] = colorChannelToByte(c.b);
    m_pixels[idx + 3] = colorChannelToByte(c.a);
    m_dirty = true;
    return true;
}

bool Canvas::getPixel(int x, int y, Color4& out) {
    ensureBuffer();
    if (x < 0 || y < 0 || x >= Width || y >= Height) return false;
    int row = Height - 1 - y;
    size_t idx = (size_t(row) * size_t(Width) + size_t(x)) * 4;
    out.r = m_pixels[idx + 0] / 255.0f;
    out.g = m_pixels[idx + 1] / 255.0f;
    out.b = m_pixels[idx + 2] / 255.0f;
    out.a = m_pixels[idx + 3] / 255.0f;
    return true;
}

void Canvas::clear(const Color4& c) {
    ensureBuffer();
    unsigned char br = colorChannelToByte(c.r);
    unsigned char bg = colorChannelToByte(c.g);
    unsigned char bb = colorChannelToByte(c.b);
    unsigned char ba = colorChannelToByte(c.a);
    for (size_t i = 0; i < m_pixels.size(); i += 4) {
        m_pixels[i + 0] = br;
        m_pixels[i + 1] = bg;
        m_pixels[i + 2] = bb;
        m_pixels[i + 3] = ba;
    }
    m_dirty = true;
}

bool Canvas::worldToUV(const Vector3& worldPos, Vector2& out) const {
    auto parent = Parent.lock();
    if (!parent || !parent->IsA("BaseCube")) return false;
    BaseCube* cube = static_cast<BaseCube*>(parent.get());

    CFrame cframe = cube->getWorldCFrame();
    Vector3 local = cframe.inverse().pointToWorld(worldPos);

    if (cube->Size.x == 0.f || cube->Size.y == 0.f || cube->Size.z == 0.f) return false;
    Vector3 localUnit = local / cube->Size;

    const float EPS = 0.05f;
    // 面の位置はレンダリング(createCubeVerticesのfaces表)に合わせる: Front=-Z, Back=+Z。
    // hitTestSurfaceGuiはFront/Backの平面が逆（描画と不一致の既存実装）なので踏襲しない
    switch (face) {
        case Face::Front:  if (std::abs(localUnit.z + 0.5f)  >= EPS) return false; break;
        case Face::Back:   if (std::abs(localUnit.z - 0.5f)  >= EPS) return false; break;
        case Face::Top:    if (std::abs(localUnit.y - 0.5f)  >= EPS) return false; break;
        case Face::Bottom: if (std::abs(localUnit.y + 0.5f)  >= EPS) return false; break;
        case Face::Right:  if (std::abs(localUnit.x - 0.5f)  >= EPS) return false; break;
        case Face::Left:   if (std::abs(localUnit.x + 0.5f)  >= EPS) return false; break;
    }

    Vector3 uAxis, vAxis;
    switch (face) {
        case Face::Top:    uAxis = Vector3(-1, 0, 0); vAxis = Vector3(0, 0, 1);  break;
        case Face::Bottom: uAxis = Vector3(-1, 0, 0); vAxis = Vector3(0, 0, -1); break;
        case Face::Front:  uAxis = Vector3(-1, 0, 0); vAxis = Vector3(0, 1, 0);  break;
        case Face::Back:   uAxis = Vector3(1, 0, 0);  vAxis = Vector3(0, 1, 0);  break;
        case Face::Right:  uAxis = Vector3(0, 0, -1); vAxis = Vector3(0, 1, 0);  break;
        case Face::Left:   uAxis = Vector3(0, 0, 1);  vAxis = Vector3(0, 1, 0);  break;
    }
    float texU = Vector3::Dot(localUnit, uAxis) + 0.5f;
    float texV = Vector3::Dot(localUnit, vAxis) + 0.5f;
    if (texU < -0.02f || texU > 1.02f || texV < -0.02f || texV > 1.02f) return false;
    texU = std::clamp(texU, 0.0f, 1.0f);
    texV = std::clamp(texV, 0.0f, 1.0f);

    out = Vector2(texU, 1.0f - texV);
    return true;
}
