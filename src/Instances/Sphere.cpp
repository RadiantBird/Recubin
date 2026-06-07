#include <Instances/Sphere.hpp>
#include <GL/glew.h>
#include <cmath>

unsigned int Sphere::defaultTextureID = 0;
unsigned int Sphere::s_VAO = 0;
unsigned int Sphere::s_VBO = 0;
unsigned int Sphere::s_EBO = 0;
int Sphere::s_IndexCount = 0;
int Sphere::s_FaceIndexCount = 0;

static const int SPH_RES = 16;

void Sphere::initGeometry() {
    if (s_VAO != 0) return;

    std::vector<float> vbo;
    std::vector<unsigned int> ebo;

    auto pushFace = [&](Vector3 normal, Vector3 right, Vector3 up) {
        unsigned int offset = (unsigned int)vbo.size() / 8;
        for (int y = 0; y <= SPH_RES; ++y) {
            for (int x = 0; x <= SPH_RES; ++x) {
                float fx = (float)x / SPH_RES;
                float fy = (float)y / SPH_RES;
                
                // キューブの面上の点
                Vector3 p = normal * 0.5f + right * (fx - 0.5f) + up * (fy - 0.5f);
                
                // 正規化して球体にする
                Vector3 n = p.normalize();
                Vector3 pos = n * 0.5f;

                vbo.push_back(pos.x); vbo.push_back(pos.y); vbo.push_back(pos.z);
                vbo.push_back(n.x);   vbo.push_back(n.y);   vbo.push_back(n.z);
                vbo.push_back(fx);    vbo.push_back(fy);
            }
        }

        for (int y = 0; y < SPH_RES; ++y) {
            for (int x = 0; x < SPH_RES; ++x) {
                unsigned int i0 = offset + y * (SPH_RES + 1) + x;
                unsigned int i1 = i0 + 1;
                unsigned int i2 = i0 + (SPH_RES + 1);
                unsigned int i3 = i2 + 1;
                // CCW (外向き)
                ebo.push_back(i0); ebo.push_back(i2); ebo.push_back(i1);
                ebo.push_back(i1); ebo.push_back(i2); ebo.push_back(i3);
            }
        }
    };

    // Face enum の順序に合わせる: Front, Back, Top, Bottom, Right, Left
    // Front (-Z)
    pushFace(Vector3(0, 0, -1), Vector3(1, 0, 0), Vector3(0, 1, 0));
    // Back (+Z)
    pushFace(Vector3(0, 0, 1), Vector3(-1, 0, 0), Vector3(0, 1, 0));
    // Top (+Y)
    pushFace(Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1));
    // Bottom (-Y)
    pushFace(Vector3(0, -1, 0), Vector3(1, 0, 0), Vector3(0, 0, -1));
    // Right (+X)
    pushFace(Vector3(1, 0, 0), Vector3(0, 0, 1), Vector3(0, 1, 0));
    // Left (-X)
    pushFace(Vector3(-1, 0, 0), Vector3(0, 0, -1), Vector3(0, 1, 0));

    s_FaceIndexCount = SPH_RES * SPH_RES * 6;
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

Sphere::Sphere(Vector3 Pos, Vector3 Sz)
    : BaseCube(Pos, Sz)
{
    initGeometry();
}

std::string Sphere::getClassName() {
    return "Sphere";
}

bool Sphere::IsA(std::string className) {
    if (className == "Sphere") return true;
    return BaseCube::IsA(className);
}

void Sphere::draw(int modelLoc, int shaderProgram) {
    glBindVertexArray(s_VAO);

    int colorLoc = glGetUniformLocation(shaderProgram, "ourColor");
    if (colorLoc != -1) {
        glUniform4f(colorLoc, Color.r, Color.g, Color.b, Color.a);
    }

    glActiveTexture(GL_TEXTURE0);
    for (int i = 0; i < 6; ++i) {
        Face f = static_cast<Face>(i);
        glBindTexture(GL_TEXTURE_2D, getDecalTexture(f, defaultTextureID));
        glDrawElements(GL_TRIANGLES, s_FaceIndexCount, GL_UNSIGNED_INT, (void*)(uintptr_t)(i * s_FaceIndexCount * sizeof(unsigned int)));
    }
}

std::shared_ptr<Instance> Sphere::clone() const {
    auto copy = std::make_shared<Sphere>(this->Position, this->Size);
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
