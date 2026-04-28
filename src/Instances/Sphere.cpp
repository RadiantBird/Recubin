#include <Instances/Sphere.hpp>
#include <GL/glew.h>
#include <cmath>

unsigned int Sphere::defaultTextureID = 0;
unsigned int Sphere::s_VAO = 0;
unsigned int Sphere::s_VBO = 0;
unsigned int Sphere::s_EBO = 0;
int Sphere::s_IndexCount = 0;

static const int SPH_LAT = 32;
static const int SPH_LON = 32;
static const float SPH_PI = 3.14159265358979323846f;

void Sphere::initGeometry() {
    if (s_VAO != 0) return;

    std::vector<float> vbo;
    std::vector<unsigned int> ebo;

    for (int lat = 0; lat <= SPH_LAT; ++lat) {
        float theta = SPH_PI * lat / SPH_LAT;
        float sinT  = std::sin(theta);
        float cosT  = std::cos(theta);

        for (int lon = 0; lon <= SPH_LON; ++lon) {
            float phi  = 2.f * SPH_PI * lon / SPH_LON;
            float sinP = std::sin(phi);
            float cosP = std::cos(phi);

            float nx = sinT * cosP;
            float ny = cosT;
            float nz = sinT * sinP;

            vbo.push_back(0.5f * nx);
            vbo.push_back(0.5f * ny);
            vbo.push_back(0.5f * nz);
            vbo.push_back(nx);
            vbo.push_back(ny);
            vbo.push_back(nz);
            vbo.push_back((float)lon / SPH_LON);
            vbo.push_back((float)lat / SPH_LAT);
        }
    }

    for (int lat = 0; lat < SPH_LAT; ++lat) {
        for (int lon = 0; lon < SPH_LON; ++lon) {
            unsigned int tl = lat * (SPH_LON + 1) + lon;
            unsigned int tr = tl + 1;
            unsigned int bl = tl + (SPH_LON + 1);
            unsigned int br = bl + 1;
            ebo.push_back(tl); ebo.push_back(bl); ebo.push_back(tr);
            ebo.push_back(tr); ebo.push_back(bl); ebo.push_back(br);
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

Sphere::Sphere(Vector3 Pos, Vector3 Sz)
    : BaseCube(Pos, Sz)
{
    initGeometry();
}

std::string Sphere::GetClassName() {
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
    glBindTexture(GL_TEXTURE_2D, defaultTextureID);
    glDrawElements(GL_TRIANGLES, s_IndexCount, GL_UNSIGNED_INT, nullptr);
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
