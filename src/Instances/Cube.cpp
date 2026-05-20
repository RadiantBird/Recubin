#include <Instances/Cube.hpp>
#include <Instances/SurfaceGui.hpp>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

unsigned int Cube::defaultTextureID = 0;
unsigned int Cube::s_VAO = 0;
unsigned int Cube::s_EBO = 0;

// 頂点生成関数の実装
std::vector<Vertex> createCubeVertices(float size) {
    float h = size / 2.0f;
    std::vector<Vertex> v;

    struct Face { float nx, ny, nz; };
    Face faces[6] = {
        { 0, 0,-1}, { 0, 0, 1}, // Front, Back
        { 0, 1, 0}, { 0,-1, 0}, // Top, Bottom
        { 1, 0, 0}, {-1, 0, 0}  // Right, Left
    };

    for (int i = 0; i < 6; i++) {
        float nx = faces[i].nx, ny = faces[i].ny, nz = faces[i].nz;
        
        // 【修正】各面におけるテクスチャの「右方向(u)」と「上方向(v)」を厳密に定義
        Vector3 u, vv;
        if (ny > 0) { // Top
            u = Vector3(1, 0, 0); vv = Vector3(0, 0, 1);
        } else if (ny < 0) { // Bottom
            u = Vector3(1, 0, 0); vv = Vector3(0, 0, -1);
        } else { // 垂直な4面 (Front, Back, Right, Left)
            // 法線に対する右方向を計算
            u = Vector3(-nz, 0, nx); 
            // 上方向は常に Y+
            vv = Vector3(0, 1, 0); 
        }

        float p[4][2] = {{1.0f,1.0f}, {1.0f,-1.0f}, {-1.0f,-1.0f}, {-1.0f,1.0f}};
        
        // Renderer.cpp (stbi_set_flip_vertically_on_load(true)) に合わせたUV
        float uv[4][2] = {
            {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 1.0f}
        };

        for (int j = 0; j < 4; j++) {
            Vertex vert;
            // 明示的に定義した u, vv ベクトルを使用して位置を計算
            vert.Position.x = (nx + p[j][0] * u.x + p[j][1] * vv.x) * h;
            vert.Position.y = (ny + p[j][0] * u.y + p[j][1] * vv.y) * h;
            vert.Position.z = (nz + p[j][0] * u.z + p[j][1] * vv.z) * h;

            vert.Normal.x = nx; vert.Normal.y = ny; vert.Normal.z = nz;
            vert.U = uv[j][0]; vert.V = uv[j][1];
            v.push_back(vert);
        }
    }
    return v;
}

// Cubeコンストラクタの実装
Cube::Cube(Vector3 Pos, Vector3 Sz, unsigned int defaultTex) 
    : BaseCube(Pos, Sz) 
{
    // faceTextures は廃止
}

std::string Cube::GetClassName() {
    return "Cube";
}

bool Cube::IsA(std::string className) {
    if (className == "Cube") {
        return true;
    }
    return BaseCube::IsA(className);
}

// 描画の実装
void Cube::draw(int modelLoc, int shaderProgram) {
    glBindVertexArray(s_VAO);
    int colorLoc        = glGetUniformLocation(shaderProgram, "ourColor");
    int uvScaleLoc      = glGetUniformLocation(shaderProgram, "uvScale");
    int isSurfaceGuiLoc = glGetUniformLocation(shaderProgram, "isSurfaceGui");

    // フェイスごとのデカール・テクスチャ収集
    unsigned int activeTextures[6];
    Decal*       activeDecals[6];
    Texture*     activeTexInst[6];
    bool         activeSurfaceGui[6];
    for (int i = 0; i < 6; i++) {
        activeTextures[i]   = defaultTextureID;
        activeDecals[i]     = nullptr;
        activeTexInst[i]    = nullptr;
        activeSurfaceGui[i] = false;
    }

    for (auto const& [name, child] : getChildren()) {
        if (child->IsA("Decal")) {
            Decal* decal = static_cast<Decal*>(child.get());
            int idx = static_cast<int>(decal->face);
            if (idx >= 0 && idx < 6) {
                activeTextures[idx] = decal->TextureID;
                activeDecals[idx]   = decal;
            }
        } else if (child->IsA("Texture")) {
            Texture* tex = static_cast<Texture*>(child.get());
            int idx = static_cast<int>(tex->face);
            if (idx >= 0 && idx < 6) {
                activeTexInst[idx] = tex;
                if (!activeDecals[idx])
                    activeTextures[idx] = tex->TextureID;
            }
        } else if (child->GetClassName() == "SurfaceGui") {
            auto* sg = static_cast<SurfaceGui*>(child.get());
            int idx = static_cast<int>(sg->face);
            if (idx >= 0 && idx < 6 && sg->m_texID != 0 && !activeDecals[idx]) {
                activeTextures[idx]   = sg->m_texID;
                activeSurfaceGui[idx] = true;
            }
        }
    }

    // フェイスごとのサイズ (u軸, v軸) — StudsPerTile の計算に使用
    // 0 Front(Z-), 1 Back(Z+): X幅, Y高さ
    // 2 Top(Y+), 3 Bottom(Y-): X幅, Z高さ
    // 4 Right(X+), 5 Left(X-): Z幅, Y高さ
    float faceSizeU[6] = { Size.x, Size.x, Size.x, Size.x, Size.z, Size.z };
    float faceSizeV[6] = { Size.y, Size.y, Size.z, Size.z, Size.y, Size.y };

    for (int i = 0; i < 6; i++) {
        if (activeDecals[i]) {
            const Color4& dc = activeDecals[i]->Color;
            if (colorLoc        != -1) glUniform4f(colorLoc,        dc.r, dc.g, dc.b, dc.a);
            if (uvScaleLoc      != -1) glUniform2f(uvScaleLoc,      1.0f, 1.0f);
            if (isSurfaceGuiLoc != -1) glUniform1f(isSurfaceGuiLoc, 0.0f);
        } else if (activeTexInst[i]) {
            const Color4& tc = activeTexInst[i]->Color;
            float su = activeTexInst[i]->StudsPerTileU;
            float sv = activeTexInst[i]->StudsPerTileV;
            float scaleU = (su > 0.0f) ? faceSizeU[i] / su : 1.0f;
            float scaleV = (sv > 0.0f) ? faceSizeV[i] / sv : 1.0f;
            if (colorLoc        != -1) glUniform4f(colorLoc,        tc.r, tc.g, tc.b, tc.a);
            if (uvScaleLoc      != -1) glUniform2f(uvScaleLoc,      scaleU, scaleV);
            if (isSurfaceGuiLoc != -1) glUniform1f(isSurfaceGuiLoc, 0.0f);
        } else if (activeSurfaceGui[i]) {
            if (colorLoc        != -1) glUniform4f(colorLoc,        Color.r, Color.g, Color.b, Color.a);
            if (uvScaleLoc      != -1) glUniform2f(uvScaleLoc,      1.0f, 1.0f);
            if (isSurfaceGuiLoc != -1) glUniform1f(isSurfaceGuiLoc, 1.0f);
        } else {
            if (colorLoc        != -1) glUniform4f(colorLoc,        Color.r, Color.g, Color.b, Color.a);
            if (uvScaleLoc      != -1) glUniform2f(uvScaleLoc,      1.0f, 1.0f);
            if (isSurfaceGuiLoc != -1) glUniform1f(isSurfaceGuiLoc, 0.0f);
        }

        glActiveTexture(GL_TEXTURE0);
        unsigned int tex = activeTextures[i];
        if (tex == 0) tex = defaultTextureID;
        glBindTexture(GL_TEXTURE_2D, tex);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, (void*)(uintptr_t)(i * 6 * sizeof(unsigned int)));
    }
}

std::shared_ptr<Instance> Cube::clone() const {
    auto copy = std::make_shared<Cube>(this->Position, this->Size, Cube::defaultTextureID);
    copy->Name     = this->Name;
    copy->Color    = this->Color;
    copy->Anchored = this->Anchored;
    copy->CanCollide = this->CanCollide;
    copy->cframe   = this->cframe;
    for (auto const& [name, child] : children) {
        copy->addChild(child->clone());
    }
    return copy;
}
