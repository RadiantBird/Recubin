#include <include/Instances/Cube.hpp>
#include <include/GL/glew.h>
#include <include/GLFW/glfw3.h>

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
        
        float ux = (nx == 0) ? 1 : 0;
        float uy = (nx != 0 || nz != 0) ? 0 : 1;
        float uz = (nz == 0 && nx != 0) ? 1 : 0;
        if (ny != 0) { ux = 1; uy = 0; uz = 0; } 
        
        float vx = ny * uz - nz * uy;
        float vy = nz * ux - nx * uz;
        float vz = nx * uy - ny * ux;

        float p[4][2] = {{1,1}, {1,-1}, {-1,-1}, {-1,1}};
        float uv[4][2] = {{1,1}, {1,0}, {0,0}, {0,1}}; 

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
    // ここからは「家が建った後」の作業
    this->ClassName = "Cube"; // 自分は Cube だと名乗る
    
    for(int i = 0; i < 6; i++) {
        faceTextures[i] = defaultTex;
    }
}

// テクスチャ設定の実装
void Cube::setFaceTexture(int faceIdx, unsigned int texID) {
    if (faceIdx >= 0 && faceIdx < 6) faceTextures[faceIdx] = texID;
}

// 描画の実装
void Cube::draw(int modelLoc, int shaderProgram) {
    Matrix4 translation = Matrix4::Translate(Position.x, Position.y, Position.z);
    Matrix4 scaling = Matrix4::Scale(Size.x, Size.y, Size.z);
    Matrix4 model = translation * scaling; 
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, model.m);

    int colorLoc = glGetUniformLocation(shaderProgram, "ourColor");
    if (colorLoc != -1) {
        glUniform4f(colorLoc, Color.r, Color.g, Color.b, Color.a);
    }

    for (int i = 0; i < 6; i++) {
        glActiveTexture(GL_TEXTURE0); 
        glBindTexture(GL_TEXTURE_2D, faceTextures[i]);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, (void*)(uintptr_t)(i * 6 * sizeof(unsigned int)));
    }
}