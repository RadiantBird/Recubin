#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <Instances/MeshCube.hpp>
#include <Core/Renderer.hpp>
#include <Core/Physics.hpp>
#include <Util/Logger.hpp>
#include <Util/AssetGuard.hpp>
#include <GL/glew.h>
#include <algorithm>

MeshCube::MeshCube(Vector3 Pos, Vector3 Sz)
    : Named<MeshCube, BaseCube>(Pos, Sz) {}

MeshCube::~MeshCube() {
    releaseGPU();
}

bool MeshCube::IsA(std::string className) {
    if (className == "MeshCube") return true;
    return BaseCube::IsA(className);
}

void MeshCube::releaseGPU() {
    if (m_EBO) { glDeleteBuffers(1, &m_EBO); m_EBO = 0; }
    if (m_VBO) { glDeleteBuffers(1, &m_VBO); m_VBO = 0; }
    if (m_VAO) { glDeleteVertexArrays(1, &m_VAO); m_VAO = 0; }
    if (m_textureID) { glDeleteTextures(1, &m_textureID); m_textureID = 0; }
    m_indexCount = 0;
}

void MeshCube::uploadToGPU() {
    std::vector<float> vbo;
    vbo.reserve(m_cpuVertices.size() * 9);
    for (auto const& v : m_cpuVertices) {
        vbo.push_back(v.Position.x); vbo.push_back(v.Position.y); vbo.push_back(v.Position.z);
        vbo.push_back(v.Normal.x);   vbo.push_back(v.Normal.y);   vbo.push_back(v.Normal.z);
        vbo.push_back(v.U);          vbo.push_back(v.V);
        vbo.push_back(v.MatAlpha);
    }

    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glGenBuffers(1, &m_EBO);

    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, vbo.size() * sizeof(float), vbo.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_cpuIndices.size() * sizeof(unsigned int), m_cpuIndices.data(), GL_STATIC_DRAW);

    GLsizei stride = 9 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(4);

    glBindVertexArray(0);

    m_indexCount = static_cast<unsigned int>(m_cpuIndices.size());
}

void MeshCube::draw(int modelLoc, int shaderProgram) {
    if (m_VAO == 0) return;
    glBindVertexArray(m_VAO);

    int colorLoc = glGetUniformLocation(shaderProgram, "ourColor");
    if (colorLoc != -1) {
        glUniform4f(colorLoc, Color.r, Color.g, Color.b, Color.a);
    }

    glActiveTexture(GL_TEXTURE0);
    unsigned int tex = (m_textureID != 0) ? m_textureID : (Renderer::instance ? Renderer::instance->whiteTexture : 0);
    glBindTexture(GL_TEXTURE_2D, tex);

    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
}

std::vector<physx::PxVec3> MeshCube::getConvexVertices() const {
    std::vector<physx::PxVec3> out;
    out.reserve(m_cpuVertices.size());
    for (auto const& v : m_cpuVertices) {
        out.push_back(physx::PxVec3(v.Position.x, v.Position.y, v.Position.z));
    }
    return out;
}

static Vector3 glbTransformPoint(const float m[16], const Vector3& p) {
    return Vector3(
        m[0]*p.x + m[4]*p.y + m[8]*p.z  + m[12],
        m[1]*p.x + m[5]*p.y + m[9]*p.z  + m[13],
        m[2]*p.x + m[6]*p.y + m[10]*p.z + m[14]
    );
}

static Vector3 glbTransformNormal(const float m[16], const Vector3& n) {
    float nx = m[0]*n.x + m[4]*n.y + m[8]*n.z;
    float ny = m[1]*n.x + m[5]*n.y + m[9]*n.z;
    float nz = m[2]*n.x + m[6]*n.y + m[10]*n.z;
    float len = std::sqrt(nx*nx + ny*ny + nz*nz);
    if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }
    return Vector3(nx, ny, nz);
}

bool MeshCube::loadFromGLB(const std::string& path) {
    if (path.size() < 4 || path.compare(path.size() - 4, 4, ".glb") != 0) {
        RCBN_WARN("MeshCube: GLB以外のファイルは未対応です: " << path);
        return false;
    }
    if (!AssetGuard::allow(path)) return false;

    cgltf_options options = {};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success) {
        RCBN_WARN("MeshCube: GLBの解析に失敗しました: " << path);
        return false;
    }
    if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success) {
        RCBN_WARN("MeshCube: GLBのバッファ読み込みに失敗しました: " << path);
        cgltf_free(data);
        return false;
    }

    std::vector<MeshVertex>   vertices;
    std::vector<unsigned int> indices;
    unsigned int loadedTexture = 0;

    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node* node = &data->nodes[ni];
        if (!node->mesh) continue;

        float worldMat[16];
        cgltf_node_transform_world(node, worldMat);

        const cgltf_mesh& mesh = *node->mesh;
        for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi) {
            const cgltf_primitive& prim = mesh.primitives[pi];
            const cgltf_accessor* posAcc = cgltf_find_accessor(&prim, cgltf_attribute_type_position, 0);
            if (!posAcc) continue;
            const cgltf_accessor* normAcc = cgltf_find_accessor(&prim, cgltf_attribute_type_normal, 0);
            const cgltf_accessor* uvAcc   = cgltf_find_accessor(&prim, cgltf_attribute_type_texcoord, 0);

            unsigned int baseIndex = static_cast<unsigned int>(vertices.size());
            for (cgltf_size vi = 0; vi < posAcc->count; ++vi) {
                MeshVertex v;
                float p[3] = {0.0f, 0.0f, 0.0f};
                cgltf_accessor_read_float(posAcc, vi, p, 3);
                v.Position = glbTransformPoint(worldMat, Vector3(p[0], p[1], p[2]));

                if (normAcc) {
                    float n[3] = {0.0f, 1.0f, 0.0f};
                    cgltf_accessor_read_float(normAcc, vi, n, 3);
                    v.Normal = glbTransformNormal(worldMat, Vector3(n[0], n[1], n[2]));
                } else {
                    v.Normal = Vector3(0.0f, 1.0f, 0.0f);
                }

                if (uvAcc) {
                    float uv[2] = {0.0f, 0.0f};
                    cgltf_accessor_read_float(uvAcc, vi, uv, 2);
                    v.U = uv[0]; v.V = 1.0f - uv[1];
                } else {
                    v.U = 0.0f; v.V = 0.0f;
                }

                // マテリアルのbaseColorFactor.a(透過素材のアルファ)を頂点に格納する
                if (prim.material && prim.material->has_pbr_metallic_roughness) {
                    v.MatAlpha = prim.material->pbr_metallic_roughness.base_color_factor[3];
                }

                vertices.push_back(v);
            }

            if (prim.indices) {
                for (cgltf_size ii = 0; ii < prim.indices->count; ++ii) {
                    indices.push_back(baseIndex + static_cast<unsigned int>(cgltf_accessor_read_index(prim.indices, ii)));
                }
            } else {
                for (cgltf_size vi = 0; vi < posAcc->count; ++vi) {
                    indices.push_back(baseIndex + static_cast<unsigned int>(vi));
                }
            }

            // 最初に見つかった埋め込みテクスチャのみ使用する(複数マテリアルは対象外)
            if (loadedTexture == 0 && prim.material && prim.material->has_pbr_metallic_roughness) {
                const cgltf_texture* tex = prim.material->pbr_metallic_roughness.base_color_texture.texture;
                if (tex && tex->image && tex->image->buffer_view) {
                    const cgltf_buffer_view* bv = tex->image->buffer_view;
                    const unsigned char* bytes = static_cast<const unsigned char*>(bv->buffer->data) + bv->offset;
                    if (Renderer::instance) {
                        loadedTexture = Renderer::instance->loadTextureFromMemory(bytes, bv->size);
                    }
                }
            }
        }
    }

    cgltf_free(data);

    if (vertices.empty() || indices.empty()) {
        RCBN_WARN("MeshCube: GLBに有効なメッシュがありません: " << path);
        if (loadedTexture) glDeleteTextures(1, &loadedTexture);
        return false;
    }

    // バウンディングボックスを計算し、最大軸が1.0になるよう正規化・中心化する
    // (Size プロパティが他のプリミティブと同じ意味で実寸を決めるようにするため)
    Vector3 minP = vertices[0].Position, maxP = vertices[0].Position;
    for (auto const& v : vertices) {
        minP.x = std::min(minP.x, v.Position.x); maxP.x = std::max(maxP.x, v.Position.x);
        minP.y = std::min(minP.y, v.Position.y); maxP.y = std::max(maxP.y, v.Position.y);
        minP.z = std::min(minP.z, v.Position.z); maxP.z = std::max(maxP.z, v.Position.z);
    }
    Vector3 center = (minP + maxP) * 0.5f;
    Vector3 extent = maxP - minP;
    float maxAxis = std::max({extent.x, extent.y, extent.z});
    float invScale = (maxAxis > 1e-6f) ? (1.0f / maxAxis) : 1.0f;
    for (auto& v : vertices) {
        v.Position = (v.Position - center) * invScale;
    }

    releaseGPU();
    m_cpuVertices = std::move(vertices);
    m_cpuIndices  = std::move(indices);
    m_textureID   = loadedTexture;
    uploadToGPU();

    return true;
}

void MeshCube::setProperty(const std::string& name, const YAML::Node& value) {
    if (name == "MeshFile") {
        std::string path = value.as<std::string>();
        if (loadFromGLB(path)) {
            MeshFile = path;
            if (lastWorkspace && lastWorkspace->getPhysicsEngine()) {
                lastWorkspace->getPhysicsEngine()->recreateActor(
                    std::static_pointer_cast<BaseCube>(shared_from_this()));
            }
        }
    } else {
        BaseCube::setProperty(name, value);
    }
}

std::shared_ptr<Instance> MeshCube::clone() const {
    auto copy = std::make_shared<MeshCube>(this->Position, this->Size);
    copy->Name       = this->Name;
    copy->Color      = this->Color;
    copy->Anchored   = this->Anchored;
    copy->CanCollide = this->CanCollide;
    copy->cframe     = this->cframe;
    if (!this->MeshFile.empty() && copy->loadFromGLB(this->MeshFile)) {
        copy->MeshFile = this->MeshFile;
    }
    for (auto const& [name, child] : children) {
        copy->addChild(child->clone());
    }
    return copy;
}
