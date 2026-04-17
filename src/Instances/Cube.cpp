#include <Instances/Cube.hpp>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

unsigned int Cube::defaultTextureID = 0;

// 頂点生成関数の実装
std::vector<Vertex> createCubeVertices(float size) {
    float h = size / 2.0f;
    std::vector<Vertex> v;

    struct Face { float nx, ny, nz; };
    Face faces[6] = {
        { 0, 0, 1}, { 0, 0,-1}, // 前, 後
        { 0, 1, 0}, { 0,-1, 0}, // 上, 下
        { 1, 0, 0}, {-1, 0, 0}  // 右, 左
    };

    for (int i = 0; i < 6; i++) {
        float nx = faces[i].nx, ny = faces[i].ny, nz = faces[i].nz;
        
        float ux = (nx == 0) ? 1.0f : 0.0f;
        float uy = (nx != 0 || nz != 0) ? 0.0f : 1.0f;
        float uz = (nz == 0 && nx != 0) ? 1.0f : 0.0f;
        if (ny != 0) { ux = 1.0f; uy = 0.0f; uz = 0.0f; } 
        
        float vx = ny * uz - nz * uy;
        float vy = nz * ux - nx * uz;
        float vz = nx * uy - ny * ux;

        float p[4][2] = {{1.0f,1.0f}, {1.0f,-1.0f}, {-1.0f,-1.0f}, {-1.0f,1.0f}};
        float uv[4][2] = {{1.0f,1.0f}, {1.0f,0.0f}, {0.0f,0.0f}, {0.0f,1.0f}}; 

        for (int j = 0; j < 4; j++) {
            Vertex vert;
            vert.Position.x = nx * h + p[j][0] * ux * h + p[j][1] * vx * h;
            vert.Position.y = ny * h + p[j][0] * uy * h + p[j][1] * vy * h;
            vert.Position.z = nz * h + p[j][0] * uz * h + p[j][1] * vz * h;

            vert.Normal.x = nx;
            vert.Normal.y = ny;
            vert.Normal.z = nz;

            vert.U = uv[j][0];
            vert.V = uv[j][1];

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
    int colorLoc = glGetUniformLocation(shaderProgram, "ourColor");
    if (colorLoc != -1) {
        glUniform4f(colorLoc, Color.r, Color.g, Color.b, Color.a);
    }

    // デカールの収集
    unsigned int activeTextures[6];
    for(int i = 0; i < 6; i++) activeTextures[i] = defaultTextureID;

    for (auto const& [name, child] : getChildren()) {
        if (child->IsA("Decal")) {
            Decal* decal = static_cast<Decal*>(child);
            int idx = static_cast<int>(decal->face);
            if (idx >= 0 && idx < 6) {
                activeTextures[idx] = decal->TextureID;
            }
        }
    }

    for (int i = 0; i < 6; i++) {
        glActiveTexture(GL_TEXTURE0); 
        unsigned int tex = activeTextures[i];
        if (tex == 0) tex = defaultTextureID;
        glBindTexture(GL_TEXTURE_2D, tex);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, (void*)(uintptr_t)(i * 6 * sizeof(unsigned int)));
    }
}
