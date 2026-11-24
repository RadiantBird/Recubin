#include <Core/SdlScenePreview.hpp>

#include <Core/SceneRuntime.hpp>
#include <Core/SdlBasicRenderer.hpp>
#include <Core/PrimitiveMeshData.hpp>
#include <Core/SdlPostEffects.hpp>
#include <Core/TerrainStreamer.hpp>
#ifdef RECUBIN_SDL_GPU_STUDIO
#include <Core/SdlPreviewImGui.hpp>
#endif
#include <Instances/BaseCube.hpp>
#include <Instances/Cube.hpp>
#include <Instances/Cylinder.hpp>
#include <Instances/Sphere.hpp>
#include <Instances/TriangularPrism.hpp>
#include <Instances/Wedge.hpp>
#include <Util/Color4.hpp>
#include <Instances/MeshCube.hpp>
#include <Instances/Lighting.hpp>
#include <Instances/Sun.hpp>
#include <Instances/Workspace.hpp>
#include <Core/User.hpp>
#include <Math/Matrix4.hpp>
#include <Util/PngWriter.hpp>
#include <include/stb_image.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <numbers>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
constexpr unsigned PREVIEW_WIDTH = 1280;
constexpr unsigned PREVIEW_HEIGHT = 720;

constexpr int WEDGE_SHAPE = 4;

int primitiveShape(const BaseCube& cube) {
    const int shape = cube.renderShapeIndex();
    if (shape >= 0) return shape;
    return dynamic_cast<const Wedge*>(&cube) ? WEDGE_SHAPE : -1;
}

std::uint32_t uploadPrimitive(SdlBasicRenderer& renderer, PrimitiveMeshData mesh,
                              std::string& error) {
    constexpr auto stride = PrimitiveMeshData::FLOATS_PER_VERTEX;
    std::vector<SdlBasicVertex> vertices;
    vertices.reserve(mesh.vertices.size() / stride);
    for (std::size_t offset = 0; offset < mesh.vertices.size(); offset += stride) {
        const float* vertex = mesh.vertices.data() + offset;
        vertices.push_back({{vertex[0], vertex[1], vertex[2]}, {vertex[6], 1.0f - vertex[7]}});
    }

    // Legacy primitives bypass back-face culling, so some of their triangles
    // face inward. Match the authored normals in the GPU mesh.
    const auto vectorAt = [&](std::uint32_t index, std::size_t offset) {
        const auto start = index * stride + offset;
        return Vector3(mesh.vertices[start], mesh.vertices[start + 1], mesh.vertices[start + 2]);
    };
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        const auto a = mesh.indices[i];
        const auto b = mesh.indices[i + 1];
        const auto c = mesh.indices[i + 2];
        const auto normal = vectorAt(a, 3) + vectorAt(b, 3) + vectorAt(c, 3);
        const auto cross = Vector3::Cross(vectorAt(b, 0) - vectorAt(a, 0),
                                          vectorAt(c, 0) - vectorAt(a, 0));
        if (Vector3::Dot(cross, normal) < 0.0f) {
            std::swap(mesh.indices[i + 1], mesh.indices[i + 2]);
        }
    }
    return renderer.uploadMesh(vertices, mesh.indices, error);
}

struct PreviewGeometry {
    Matrix4 model;
    const MeshCube* mesh;
};

class TerrainGpuMeshes {
public:
    bool release(SdlBasicRenderer& renderer, std::string& error) {
        for (const auto& [key, mesh] : m_meshes) {
            if (mesh.handle && !renderer.releaseMesh(mesh.handle, error)) return false;
        }
        m_meshes.clear();
        return true;
    }

    bool sync(const TerrainStreamer& streamer, SdlBasicRenderer& renderer, std::string& error) {
        const auto& chunks = streamer.getChunks();
        for (auto it = m_meshes.begin(); it != m_meshes.end();) {
            if (chunks.contains(it->first)) {
                ++it;
                continue;
            }
            if (it->second.handle && !renderer.releaseMesh(it->second.handle, error)) {
                return false;
            }
            it = m_meshes.erase(it);
        }
        for (const auto& [key, entry] : chunks) {
            if (!entry.cpuGeometry) {
                continue;
            }
            auto& mesh = m_meshes[key];
            if (mesh.revision == entry.geometryRevision) {
                continue;
            }
            std::uint32_t replacement = 0;
            if (!entry.cpuGeometry->indices.empty()) {
                std::vector<SdlBasicVertex> vertices;
                vertices.reserve(entry.cpuGeometry->vertices.size());
                for (const auto& v : entry.cpuGeometry->vertices) {
                    vertices.push_back({{v.x, v.y, v.z}, {v.u, v.v}, {v.r, v.g, v.b, v.a}});
                }
                replacement = renderer.uploadMesh(vertices, entry.cpuGeometry->indices, error);
                if (!replacement) {
                    return false;
                }
            }
            if (mesh.handle && !renderer.releaseMesh(mesh.handle, error)) {
                return false;
            }
            mesh = {entry.geometryRevision, replacement};
        }
        return true;
    }

    void appendDraws(const Matrix4& viewProjection, const std::optional<Matrix4>& shadowProjection,
                     std::vector<SdlBasicCube>& draws) const {
        for (const auto& [key, mesh] : m_meshes) {
            if (mesh.handle) {
                auto& draw = draws.emplace_back();
                draw.geometry = mesh.handle;
                // Terrain faces authored for the legacy renderer have mixed winding.
                draw.doubleSided = true;
                std::copy_n(viewProjection.m, 16, draw.modelViewProjection.begin());
                if (shadowProjection) {
                    std::copy_n(shadowProjection->m, 16, draw.shadowModelViewProjection.begin());
                }
            }
        }
    }

private:
    struct Mesh {
        std::uint64_t revision = 0;
        std::uint32_t handle = 0;
    };
    struct ChunkOrder {
        bool operator()(const TerrainStreamer::ChunkKey& a, const TerrainStreamer::ChunkKey& b) const {
            return std::tie(a.cx, a.cy, a.cz) < std::tie(b.cx, b.cy, b.cz);
        }
    };
    std::map<TerrainStreamer::ChunkKey, Mesh, ChunkOrder> m_meshes;
};

class PreviewTerrain {
public:
    explicit PreviewTerrain(Terrain& terrain)
        : m_terrain(terrain),
          m_streamer(nullptr, &terrain, terrain.DataPath, static_cast<uint32_t>(terrain.Seed),
                     terrain.Flat, true) {}

    float surfaceAtOrigin() const {
        return m_streamer.findSurfaceY(0, 0) * TerrainStreamer::BLOCK_STUD_SIZE;
    }
    bool update(const Vector3& center, SdlBasicRenderer& renderer, std::string& error) {
        m_streamer.update(center);
        return m_meshes.sync(m_streamer, renderer, error);
    }
    bool isSettled() const { return m_streamer.isSettled(); }
    const std::string& name() const { return m_terrain.Name; }
    void appendDraws(const Matrix4& viewProjection, const std::optional<Matrix4>& shadowProjection,
                     std::vector<SdlBasicCube>& draws) const {
        m_meshes.appendDraws(viewProjection, shadowProjection, draws);
    }

private:
    Terrain& m_terrain;
    TerrainStreamer m_streamer;
    TerrainGpuMeshes m_meshes;
};

std::uint32_t uploadEmbeddedTexture(SdlBasicRenderer& renderer,
                                    const MeshEmbeddedTexture& texture, std::string& error) {
    if (texture.bytes.empty() || texture.bytes.size() > std::numeric_limits<int>::max()) {
        error = "invalid embedded image size";
        return 0;
    }
    int width = 0;
    int height = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(texture.bytes.data(), static_cast<int>(texture.bytes.size()),
                              &width, &height, nullptr, STBI_rgb_alpha), stbi_image_free);
    if (!pixels || width <= 0 || height <= 0) {
        const char* reason = stbi_failure_reason();
        error = reason ? reason : "could not decode the embedded image";
        return 0;
    }
    const auto bytes = static_cast<std::size_t>(width) * height * 4;
    return renderer.uploadTexture(width, height, {pixels.get(), bytes}, error);
}

Matrix4 sdlClipDepth(const Matrix4& projection) {
    // Convert the engine projection's [-1, 1] clip depth to SDL GPU's [0, 1].
    Matrix4 depthRange;
    depthRange.m[10] = 0.5f;
    depthRange.m[14] = 0.5f;
    return depthRange * projection;
}

Matrix4 sdlPerspective(float fovY, float aspect, float nearPlane, float farPlane) {
    return sdlClipDepth(Matrix4::Perspective(fovY, aspect, nearPlane, farPlane));
}

std::optional<Matrix4> shadowProjection(const Workspace& workspace, const Vector3& position,
                                       const Vector3& forward) {
    const auto& lightings = workspace.getRenderLightings();
    if (lightings.empty() || !lightings.front()) return std::nullopt;
    const float distance = lightings.front()->shadowDistance;
    if (!std::isfinite(distance) || distance <= 0.1f) return std::nullopt;
    for (const CelestialBody* body : workspace.getRenderCelestialBodies()) {
        const auto* sun = dynamic_cast<const Sun*>(body);
        if (!sun) continue;
        const Vector3 direction = Sun::directionFromAngle(sun->Angle);
        const Vector3 focus = position + forward * (distance * 0.3f);
        const Vector3 up = std::abs(direction.y) > 0.98f ? Vector3(0, 0, 1) : Vector3(0, 1, 0);
        const float extent = distance * 0.5f;
        const Matrix4 lightView = Matrix4::LookAt(focus + direction * distance, focus, up);
        return sdlClipDepth(Matrix4::Ortho(-extent, extent, -extent, extent, 0.05f, distance * 2.0f))
             * lightView;
    }
    return std::nullopt;
}

SDL_GPUTransferBuffer* recordReadback(SDL_GPUDevice* device, SDL_GPUCommandBuffer* command,
                                      SDL_GPUTexture* texture, unsigned width, unsigned height,
                                      std::string& error) {
    const std::size_t byteCount = static_cast<std::size_t>(width) * height * 4;
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    transferInfo.size = static_cast<Uint32>(byteCount);
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
    if (!transfer) {
        error = SDL_GetError();
        return nullptr;
    }

    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
    if (!copy) {
        error = SDL_GetError();
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return nullptr;
    }
    SDL_GPUTextureRegion source{texture, 0, 0, 0, 0, 0, width, height, 1};
    SDL_GPUTextureTransferInfo destination{transfer, 0, width, height};
    SDL_DownloadFromGPUTexture(copy, &source, &destination);
    SDL_EndGPUCopyPass(copy);

    return transfer;
}

bool finishReadback(SDL_GPUDevice* device, SDL_GPUTransferBuffer* transfer,
                    SDL_GPUFence* fence, unsigned width, unsigned height,
                    const std::filesystem::path& path, std::string& error) {
    if (!fence || !SDL_WaitForGPUFences(device, true, &fence, 1)) {
        error = SDL_GetError();
        if (fence) SDL_ReleaseGPUFence(device, fence);
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }
    auto* mapped = static_cast<const std::uint8_t*>(SDL_MapGPUTransferBuffer(device, transfer, false));
    if (!mapped) {
        error = SDL_GetError();
        SDL_ReleaseGPUFence(device, fence);
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
    std::vector<std::uint8_t> pixels(rowBytes * height);
    for (unsigned y = 0; y < height; ++y) {
        std::copy_n(mapped + static_cast<std::size_t>(height - 1 - y) * rowBytes,
                    rowBytes, pixels.data() + static_cast<std::size_t>(y) * rowBytes);
    }
    SDL_UnmapGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUFence(device, fence);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return PngWriter::writeRgba8(path, static_cast<int>(width), static_cast<int>(height), pixels, &error);
}

#ifdef RECUBIN_SDL_GPU_STUDIO
SDL_GPUTexture* createStudioCaptureTarget(SDL_GPUDevice* device, unsigned width,
                                          unsigned height, std::string& error) {
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    SDL_GPUTexture* target = SDL_CreateGPUTexture(device, &info);
    if (!target) error = SDL_GetError();
    return target;
}

struct StudioTextureDeleter {
    SDL_GPUDevice* device = nullptr;
    void operator()(SDL_GPUTexture* texture) const {
        if (device && texture) SDL_ReleaseGPUTexture(device, texture);
    }
};
#endif
}

struct SdlLiveScene::State {
    SdlBasicRenderer renderer;
    SdlPostEffects postEffects;
    SDL_GPUTexture* result = nullptr;
    struct MeshDraw {
        SdlBasicCube draw;
        Matrix4 model;
        const BaseCube* owner; // Borrowed from the Workspace until the next sync.
    };
    std::vector<MeshDraw> meshes;
    std::vector<SdlBasicCube> frameDraws;
    std::vector<SdlBasicCube> outlineDraws;
    std::uint32_t cubeFaces = 0;
    std::array<std::uint32_t, WEDGE_SHAPE + 1> primitiveMeshes{};
    Matrix4 viewProjection;
    bool frameSucceeded = false;
    struct MeshCache {
        std::uint32_t handle = 0;
        std::weak_ptr<const Instance> owner;
        std::uint64_t revision = 0;
        std::vector<std::uint32_t> textures;
    };
    std::unordered_map<const BaseCube*, MeshCache> meshCache;
    struct TerrainCache {
        TerrainGpuMeshes meshes;
        std::weak_ptr<const Instance> owner;
        std::uint64_t streamRevision = 0;
    };
    std::unordered_map<const Terrain*, TerrainCache> terrainCache;
    bool initialized = false;

    bool releaseCache(MeshCache& cache, std::string& error) {
        if (cache.handle && !renderer.releaseMesh(cache.handle, error)) return false;
        for (auto texture : cache.textures) {
            if (texture && !renderer.releaseTexture(texture, error)) return false;
        }
        return true;
    }
};

SdlLiveScene::SdlLiveScene() : m_state(std::make_unique<State>()) {}
SdlLiveScene::~SdlLiveScene() = default;

bool SdlLiveScene::initialize(SDL_GPUDevice& device, std::string& error) {
    if (!m_state->renderer.initialize(device, error)) return false;
    if (!m_state->postEffects.initialize(device, error)) return false;
    std::vector<SdlBasicVertex> vertices;
    for (const Vertex& vertex : createCubeVertices(1.0f)) {
        vertices.push_back({{vertex.Position.x, vertex.Position.y, vertex.Position.z},
                            {vertex.U, 1.0f - vertex.V}});
    }
    std::vector<std::uint32_t> indices;
    for (std::uint32_t face = 0; face < vertices.size(); face += 4) {
        indices.insert(indices.end(), {face, face + 2, face + 1, face, face + 3, face + 2});
    }
    m_state->cubeFaces = m_state->renderer.uploadMesh(vertices, indices, error);
    if (!m_state->cubeFaces) return false;
    const std::array builders{&Cylinder::buildGeometry, &Sphere::buildGeometry,
                              &TriangularPrism::buildGeometry, &Wedge::buildGeometry};
    for (std::size_t i = 0; i < builders.size(); ++i) {
        const auto handle = uploadPrimitive(m_state->renderer, builders[i](), error);
        if (!handle) return false;
        m_state->primitiveMeshes[i + 1] = handle;
    }
    m_state->initialized = true;
    return true;
}

bool SdlLiveScene::sync(Workspace& workspace, std::string& error) {
    if (!m_state->initialized) {
        error = "SdlLiveScene: renderer is not initialized";
        return false;
    }
    m_state->meshes.clear();
    std::unordered_set<const BaseCube*> active;
    for (BaseCube* cube : workspace.getRenderBaseCubes()) {
        if (!cube) {
            error = "Workspace contains an empty render entry";
            return false;
        }
        if (cube->Color.a < 1.0f) continue;
        const auto* mesh = dynamic_cast<const MeshCube*>(cube);
        const int shape = primitiveShape(*cube);
        if (!mesh && shape < 0) continue;
        const Matrix4 model = cube->getWorldCFrame().toMatrix4() *
                              Matrix4::Scale(cube->Size.x, cube->Size.y, cube->Size.z);
        if (mesh && !mesh->isUsingFallback() && !mesh->cpuVertices().empty() &&
            !mesh->cpuIndices().empty()) {
            active.insert(cube);
            auto& cache = m_state->meshCache[cube];
            if (!cache.handle || cache.owner.expired() || cache.revision != mesh->cpuGeometryRevision()) {
                std::vector<SdlBasicVertex> vertices;
                vertices.reserve(mesh->cpuVertices().size());
                for (const auto& vertex : mesh->cpuVertices()) {
                    vertices.push_back({{vertex.Position.x, vertex.Position.y, vertex.Position.z},
                                        {vertex.U, 1.0f - vertex.V}});
                }
                State::MeshCache replacement;
                replacement.handle = m_state->renderer.uploadMesh(vertices, mesh->cpuIndices(), error);
                if (!replacement.handle) return false;
                replacement.owner = mesh->weak_from_this();
                replacement.revision = mesh->cpuGeometryRevision();
                for (const auto& image : mesh->cpuEmbeddedTextures()) {
                    const auto texture = uploadEmbeddedTexture(m_state->renderer, image, error);
                    if (!texture) return false;
                    replacement.textures.push_back(texture);
                }
                if (!m_state->releaseCache(cache, error)) return false;
                cache = std::move(replacement);
            }
            const auto geometry = cache.handle;
            const auto sections = mesh->cpuMaterialSections();
            if (sections.empty()) {
                SdlBasicCube draw;
                draw.geometry = geometry;
                draw.castShadow = cube->shouldCastShadow();
                draw.receiveShadow = !cube->Unlit;
                draw.color = {cube->Color.r, cube->Color.g, cube->Color.b, cube->Color.a};
                m_state->meshes.push_back({draw, model, cube});
            } else {
                for (const auto& section : sections) {
                    if (!section.opaque) continue;
                    SdlBasicCube draw;
                    draw.geometry = geometry;
                    draw.castShadow = cube->shouldCastShadow();
                    draw.receiveShadow = !cube->Unlit;
                    draw.firstIndex = section.firstIndex;
                    draw.indexCount = section.indexCount;
                    if (section.textureIndex > cache.textures.size()) {
                        error = "MeshCube '" + mesh->Name + "' has an invalid material texture index";
                        return false;
                    }
                    if (section.textureIndex) {
                        draw.texture = cache.textures[section.textureIndex - 1];
                    }
                    draw.color = {cube->Color.r * section.baseColor.r,
                                  cube->Color.g * section.baseColor.g,
                                  cube->Color.b * section.baseColor.b,
                                  cube->Color.a * section.baseColor.a};
                    m_state->meshes.push_back({draw, model, cube});
                }
            }
            continue;
        }
        if (mesh && !mesh->isUsingFallback()) continue;
        SdlBasicCube draw;
        if (!mesh) draw.geometry = m_state->primitiveMeshes[static_cast<std::size_t>(shape)];
        draw.castShadow = cube->shouldCastShadow(mesh && mesh->isUsingFallback());
        draw.receiveShadow = !cube->Unlit;
        draw.color = {cube->Color.r, cube->Color.g, cube->Color.b, cube->Color.a};
        m_state->meshes.push_back({draw, model, cube});
    }
    for (auto it = m_state->meshCache.begin(); it != m_state->meshCache.end();) {
        if (active.contains(it->first)) {
            ++it;
            continue;
        }
        if (!m_state->releaseCache(it->second, error)) return false;
        it = m_state->meshCache.erase(it);
    }
    std::unordered_set<const Terrain*> activeTerrains;
    for (const Terrain* terrain : workspace.getRenderTerrains()) {
        if (!terrain) {
            error = "Workspace contains an empty Terrain render entry";
            return false;
        }
        if (!terrain->Enabled || !terrain->streamer) continue;
        activeTerrains.insert(terrain);
        auto& cache = m_state->terrainCache[terrain];
        if (cache.owner.expired() || cache.streamRevision != terrain->streamerRevision()) {
            if (!cache.meshes.release(m_state->renderer, error)) return false;
            cache.owner = terrain->weak_from_this();
            cache.streamRevision = terrain->streamerRevision();
        }
        if (!cache.meshes.sync(*terrain->streamer, m_state->renderer, error)) {
            error = "Terrain '" + terrain->Name + "': " + error;
            return false;
        }
    }
    for (auto it = m_state->terrainCache.begin(); it != m_state->terrainCache.end();) {
        if (activeTerrains.contains(it->first)) {
            ++it;
            continue;
        }
        if (!it->second.meshes.release(m_state->renderer, error)) return false;
        it = m_state->terrainCache.erase(it);
    }
    return true;
}

bool SdlLiveScene::draw(Workspace& workspace, const Matrix4& viewProjection,
                        const Vector3& cameraPosition, const Vector3& cameraForward, float effectTime,
                        SDL_GPUCommandBuffer& commandBuffer, unsigned width, unsigned height,
                        std::string& error, std::span<BaseCube* const> selected,
                        const SdlSurfaceGuiLayers* surfaceLayers) {
    m_state->viewProjection = viewProjection;
    m_state->frameSucceeded = false;
    const auto lightProjection = shadowProjection(workspace, cameraPosition, cameraForward);
    auto& draws = m_state->frameDraws;
    draws.clear();
    draws.reserve(m_state->meshes.size());
    for (const auto& mesh : m_state->meshes) {
        auto draw = mesh.draw;
        const Matrix4 mvp = viewProjection * mesh.model;
        std::copy_n(mvp.m, 16, draw.modelViewProjection.begin());
        if (lightProjection) {
            const Matrix4 shadowMvp = *lightProjection * mesh.model;
            std::copy_n(shadowMvp.m, 16, draw.shadowModelViewProjection.begin());
        }
        if (surfaceLayers) {
            const auto layers = surfaceLayers->find(mesh.owner);
            if (layers != surfaceLayers->end()) {
                draw.geometry = m_state->cubeFaces;
                draw.indexCount = 6;
                for (SDL_GPUTexture* texture : layers->second) {
                    draw.surfaceTexture = texture;
                    draws.push_back(draw);
                    draw.firstIndex += draw.indexCount;
                }
                continue;
            }
        }
        draws.push_back(draw);
    }
    for (const auto& [terrain, cache] : m_state->terrainCache) {
        cache.meshes.appendDraws(viewProjection, lightProjection, draws);
    }
    const bool drawn = m_state->renderer.draw(commandBuffer, width, height, draws,
                                               error, lightProjection.has_value());
    if (!drawn) {
        return false;
    }
    if (!recordOutline(commandBuffer, *m_state->renderer.colorTarget(), selected,
                       Color4(0.25f, 0.9f, 1.0f, 1.0f), 2.0f, error) ||
        !m_state->postEffects.apply(workspace, commandBuffer,
                                               *m_state->renderer.colorTarget(), width, height,
                                               effectTime, m_state->result, error)) {
        return false;
    }
    m_state->frameSucceeded = true;
    return true;
}

bool SdlLiveScene::drawOutline(SDL_GPUCommandBuffer& commandBuffer,
                               std::span<BaseCube* const> targets,
                               const Color4& color, float thickness, std::string& error) {
    if (!m_state->frameSucceeded || !m_state->result) {
        error = "SDL scene outline requested before a successful scene frame";
        return false;
    }
    return recordOutline(commandBuffer, *m_state->result, targets, color, thickness, error);
}

bool SdlLiveScene::recordOutline(SDL_GPUCommandBuffer& commandBuffer, SDL_GPUTexture& destination,
                                std::span<BaseCube* const> targets, const Color4& color,
                                float thickness, std::string& error) {
    if (targets.empty()) {
        return true;
    }
    const std::unordered_set<const BaseCube*> targetSet(targets.begin(), targets.end());
    auto& selected = m_state->outlineDraws;
    selected.clear();
    for (const auto& mesh : m_state->meshes) {
        if (!targetSet.contains(mesh.owner)) continue;
        auto& draw = selected.emplace_back(mesh.draw);
        const Matrix4 mvp = m_state->viewProjection * mesh.model;
        std::copy_n(mvp.m, 16, draw.modelViewProjection.begin());
    }
    return selected.empty() || m_state->renderer.drawOutline(commandBuffer, destination, selected,
                                                              color, thickness, error);
}

bool SdlLiveScene::drawTerrainGuide(SDL_GPUCommandBuffer& commandBuffer, const Vector3& center,
                                    const Vector3& normal, float radius, std::string& error) {
    if (!m_state->frameSucceeded || !m_state->result) {
        error = "SDL terrain guide requested before a successful scene frame";
        return false;
    }
    const Vector3 n = normal.normalize();
    if (!std::isfinite(n.length()) || n.length() < 0.5f) {
        error = "SDL terrain guide requires a finite nonzero normal";
        return false;
    }
    const Vector3 worldUp(0.0f, 1.0f, 0.0f);
    const Vector3 worldForward(0.0f, 0.0f, 1.0f);
    const Vector3 reference = std::abs(Vector3::Dot(n, worldUp)) > 0.99f
        ? worldForward : worldUp;
    const Vector3 tangentU = Vector3::Cross(reference, n).normalize();
    const Vector3 tangentV = Vector3::Cross(n, tangentU);
    const Vector3 origin = center + n * 0.05f;
    Matrix4 model;
    model.m[0] = tangentU.x;
    model.m[1] = tangentU.y;
    model.m[2] = tangentU.z;
    model.m[4] = tangentV.x;
    model.m[5] = tangentV.y;
    model.m[6] = tangentV.z;
    model.m[8] = n.x;
    model.m[9] = n.y;
    model.m[10] = n.z;
    model.m[12] = origin.x;
    model.m[13] = origin.y;
    model.m[14] = origin.z;
    const Matrix4 guideMvp = m_state->viewProjection * model;
    std::array<float, 16> transform;
    std::copy_n(guideMvp.m, transform.size(), transform.begin());
    return m_state->renderer.drawTerrainGuide(commandBuffer, *m_state->result,
                                               transform, radius, error);
}

SDL_GPUTexture* SdlLiveScene::colorTarget() const {
    return m_state->result;
}

Matrix4 sdlSceneViewProjection(const User& user, float aspect) {
    return sdlPerspective(45.0f, aspect, 0.1f, 10000.0f) *
           Matrix4::LookAt(user.cpos, user.cpos + user.forward, user.up);
}

void blitSdlSceneFrame(SDL_GPUCommandBuffer& command, SDL_GPUTexture& source,
                       SDL_GPUTexture& destination, unsigned width, unsigned height) {
    SDL_GPUBlitInfo blit{};
    blit.source = {&source, 0, 0, 0, 0, width, height};
    blit.destination = {&destination, 0, 0, 0, 0, width, height};
    blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
    blit.filter = SDL_GPU_FILTER_LINEAR;
    SDL_BlitGPUTexture(&command, &blit);
}

bool captureSdlSceneFrame(SdlGpuWindow& window, SdlGpuWindow::Frame& frame,
                         SDL_GPUTexture* source, unsigned width, unsigned height,
                         const std::string& outputDir, std::string& error) {
    return captureSdlFrameToPng(window, frame, source, width, height,
        std::filesystem::u8path(outputDir) / "render.png", error);
}

bool captureSdlFrameToPng(SdlGpuWindow& window, SdlGpuWindow::Frame& frame,
                          SDL_GPUTexture* source, unsigned width, unsigned height,
                          const std::filesystem::path& outputPath, std::string& error) {
    auto* transfer = recordReadback(window.device(), frame.command, source, width, height, error);
    if (!transfer) {
        std::string retireError;
        if (!window.submit(frame, nullptr, &retireError)) {
            error += "; " + retireError;
        }
        return false;
    }
    SDL_GPUFence* fence = nullptr;
    if (!window.submit(frame, &fence, &error)) {
        SDL_ReleaseGPUTransferBuffer(window.device(), transfer);
        return false;
    }
    return finishReadback(window.device(), transfer, fence, width, height,
                          outputPath, error);
}

bool captureSdlTargetToPng(SDL_GPUDevice& device, SDL_GPUCommandBuffer& command,
                           SDL_GPUTexture& source, unsigned width, unsigned height,
                           const std::filesystem::path& outputPath, std::string& error) {
    auto* transfer = recordReadback(&device, &command, &source, width, height, error);
    if (!transfer) {
        if (!SDL_SubmitGPUCommandBuffer(&command)) {
            error += std::string(". Submit failed: ") + SDL_GetError();
        }
        return false;
    }
    auto* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(&command);
    return finishReadback(&device, transfer, fence, width, height, outputPath, error);
}

int runSdlScenePreview(const std::string& scenePath,
                       const std::string& title,
                       SdlGpuDriver driver,
                       const std::optional<std::string>& captureDir,
                       bool studio) {
    const auto staged = SceneRuntime::stageSceneLoad(scenePath, {}, {});
    if (!staged) {
        std::cerr << "SDL scene preview: failed to load " << scenePath << ": " << staged.message << '\n';
        return 1;
    }
    const auto workspaces = SceneRuntime::collectWorkspaces(staged.system);
    if (workspaces.empty() || !workspaces.front()) {
        std::cerr << "SDL scene preview: scene has no Workspace: " << scenePath << '\n';
        return 1;
    }

    std::vector<SdlBasicCube> cubes;
    std::vector<PreviewGeometry> geometrySources;
    Vector3 minBounds(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    Vector3 maxBounds(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
    std::size_t skipped = 0;
    for (BaseCube* cube : workspaces.front()->getRenderBaseCubes()) {
        if (!cube) {
            std::cerr << "SDL scene preview: Workspace contains an empty render entry\n";
            return 1;
        }
        const auto* mesh = dynamic_cast<const MeshCube*>(cube);
        if ((!mesh && cube->renderShapeIndex() != 0) || cube->Color.a < 1.0f) {
            ++skipped;
            continue;
        }
        if (mesh && !mesh->isUsingFallback() &&
            (mesh->cpuVertices().empty() || mesh->cpuIndices().empty())) {
            std::cerr << "SDL scene preview: MeshCube '" << mesh->Name << "' has no loaded geometry\n";
            ++skipped;
            continue;
        }
        const CFrame world = cube->getWorldCFrame();
        const Matrix4 model = world.toMatrix4() * Matrix4::Scale(cube->Size.x, cube->Size.y, cube->Size.z);
        const float halfExtent = cube->Size.length() * 0.5f;
        const Color4 color = cube->Color;
        if (!std::isfinite(halfExtent) || !std::ranges::all_of(model.m, [](float value) {
                return std::isfinite(value);
            })) {
            std::cerr << "SDL scene preview: invalid transform for Cube '" << cube->Name << "'\n";
            return 1;
        }
        if (!std::isfinite(color.r) || !std::isfinite(color.g) ||
            !std::isfinite(color.b) || !std::isfinite(color.a)) {
            std::cerr << "SDL scene preview: non-finite color for Cube '" << cube->Name << "'\n";
            return 1;
        }
        const Vector3 half(halfExtent, halfExtent, halfExtent);
        minBounds.x = std::min(minBounds.x, world.Position.x - half.x);
        minBounds.y = std::min(minBounds.y, world.Position.y - half.y);
        minBounds.z = std::min(minBounds.z, world.Position.z - half.z);
        maxBounds.x = std::max(maxBounds.x, world.Position.x + half.x);
        maxBounds.y = std::max(maxBounds.y, world.Position.y + half.y);
        maxBounds.z = std::max(maxBounds.z, world.Position.z + half.z);
        geometrySources.push_back({model, mesh});
        cubes.push_back({{}, {color.r, color.g, color.b, color.a}});
        cubes.back().castShadow = cube->shouldCastShadow(mesh && mesh->isUsingFallback());
        cubes.back().receiveShadow = !cube->Unlit;
    }
    if (skipped != 0) {
        std::cerr << "SDL scene preview: skipped " << skipped << " unsupported or transparent shapes\n";
    }

    std::vector<std::unique_ptr<PreviewTerrain>> terrains;
    for (Terrain* terrain : SceneRuntime::collectTerrains(workspaces.front().get())) {
        if (terrain->Enabled && !terrain->DataPath.empty()) {
            terrains.push_back(std::make_unique<PreviewTerrain>(*terrain));
        }
    }
    Vector3 center = cubes.empty() ? Vector3(0, 0, 0) : minBounds * 0.5f + maxBounds * 0.5f;
    constexpr float chunkWidth = CHUNK_SIZE * TerrainStreamer::BLOCK_STUD_SIZE;
    const float radius = cubes.empty() ? (terrains.empty() ? 1.0f : chunkWidth * 0.5f)
                                       : std::max(1.0f, (maxBounds - minBounds).length() * 0.5f);
    bool terrainCameraPending = cubes.empty() && !terrains.empty();
    if (terrainCameraPending) {
        center.y = terrains.front()->surfaceAtOrigin();
    }
    if (!std::isfinite(radius)) {
        std::cerr << "SDL scene preview: bounds exceed the supported camera range\n";
        return 1;
    }

    SdlGpuWindow window;
    SdlGpuWindowConfig config;
    config.title = title.c_str();
    config.width = PREVIEW_WIDTH;
    config.height = PREVIEW_HEIGHT;
    config.driver = driver;
    config.shaderFormats = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL;
    config.present = !captureDir.has_value();
    config.createWindowOffscreen = studio && captureDir.has_value();
    config.visible = !captureDir.has_value();
    std::string error;
    if (!window.initialize(config, &error)) {
        std::cerr << "SDL scene preview: GPU initialization failed: " << error << '\n';
        return 1;
    }
    SdlBasicRenderer renderer;
    if (!renderer.initialize(*window.device(), error)) {
        std::cerr << "SDL scene preview: renderer initialization failed: " << error << '\n';
        return 1;
    }
    SdlPostEffects postEffects;
    if (!postEffects.initialize(*window.device(), error)) {
        std::cerr << "SDL scene preview: post processing initialization failed: " << error << '\n';
        return 1;
    }
    std::vector<SdlBasicCube> drawItems;
    std::vector<Matrix4> drawModels;
    for (std::size_t index = 0; index < geometrySources.size(); ++index) {
        const MeshCube* mesh = geometrySources[index].mesh;
        auto item = cubes[index];
        const Matrix4& model = geometrySources[index].model;
        if (!mesh || mesh->isUsingFallback()) {
            drawItems.push_back(item);
            drawModels.push_back(model);
            continue;
        }
        std::vector<SdlBasicVertex> vertices;
        vertices.reserve(mesh->cpuVertices().size());
        for (const MeshVertex& vertex : mesh->cpuVertices()) {
            // MeshCube keeps GL-oriented UVs. The decoded SDL texture starts at its top row.
            vertices.push_back({{vertex.Position.x, vertex.Position.y, vertex.Position.z},
                                {vertex.U, 1.0f - vertex.V}});
        }
        const auto geometry = renderer.uploadMesh(vertices, mesh->cpuIndices(), error);
        if (geometry == 0) {
            std::cerr << "SDL scene preview: could not upload MeshCube '" << mesh->Name
                      << "': " << error << '\n';
            return 1;
        }
        item.geometry = geometry;
        const auto sections = mesh->cpuMaterialSections();
        const auto images = mesh->cpuEmbeddedTextures();
        std::vector<std::uint32_t> uploadedTextures(images.size());
        if (sections.empty()) {
            drawItems.push_back(item);
            drawModels.push_back(model);
        }
        std::size_t translucentSections = 0;
        for (const MeshMaterialSection& section : sections) {
            if (!section.opaque) {
                ++translucentSections;
                continue;
            }
            auto draw = item;
            draw.firstIndex = section.firstIndex;
            draw.indexCount = section.indexCount;
            draw.color[0] *= section.baseColor.r;
            draw.color[1] *= section.baseColor.g;
            draw.color[2] *= section.baseColor.b;
            if (section.textureIndex != 0) {
                if (section.textureIndex > uploadedTextures.size()) {
                    std::cerr << "SDL scene preview: invalid material texture index on '" << mesh->Name << "'\n";
                    return 1;
                }
                auto& handle = uploadedTextures[section.textureIndex - 1];
                if (handle == 0) {
                    handle = uploadEmbeddedTexture(renderer, images[section.textureIndex - 1], error);
                    if (handle == 0) {
                        std::cerr << "SDL scene preview: texture upload failed on '" << mesh->Name
                                  << "': " << error << '\n';
                        return 1;
                    }
                }
                draw.texture = handle;
            }
            drawItems.push_back(draw);
            drawModels.push_back(model);
        }
        if (translucentSections != 0) {
            std::cerr << "SDL scene preview: skipped " << translucentSections
                      << " mask/blend material sections on '" << mesh->Name << "'\n";
        }
    }

#ifdef RECUBIN_SDL_GPU_STUDIO
    std::unique_ptr<SdlPreviewImGui> studioUi;
    if (studio) {
        studioUi = std::make_unique<SdlPreviewImGui>();
        SDL_GPUTextureFormat uiFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        if (!captureDir) {
            uiFormat = SDL_GetGPUSwapchainTextureFormat(window.device(), window.window());
            if (uiFormat == SDL_GPU_TEXTUREFORMAT_INVALID) {
                error = SDL_GetError();
                std::cerr << "SDL scene preview: could not query Studio swapchain format: "
                          << error << '\n';
                return 1;
            }
        }
        if (!studioUi->initialize(*window.window(), *window.device(), uiFormat, error)) {
            std::cerr << "SDL scene preview: ImGui initialization failed: " << error << '\n';
            return 1;
        }
    }
#endif

#ifdef RECUBIN_SDL_GPU_STUDIO
    std::unique_ptr<SDL_GPUTexture, StudioTextureDeleter> studioCaptureTarget(nullptr,
                                                                                {window.device()});
#endif
    if (studio && captureDir) {
#ifdef RECUBIN_SDL_GPU_STUDIO
        studioCaptureTarget.reset(createStudioCaptureTarget(window.device(), PREVIEW_WIDTH,
                                                            PREVIEW_HEIGHT, error));
        if (!studioCaptureTarget.get()) {
            std::cerr << "SDL scene preview: cannot create Studio capture target: "
                      << error << '\n';
            return 1;
        }
#else
        std::cerr << "SDL scene preview: Studio support is not compiled in\n";
        return 1;
#endif
    }

    bool running = true;
    unsigned layoutAttempts = 0;
    unsigned previousWidth = 0;
    unsigned previousHeight = 0;
    float orbitAngle = 0.0f;
    bool cameraDirty = true;
    Matrix4 viewProjection;
    std::optional<Matrix4> lightProjection;
    const std::size_t staticDrawCount = drawItems.size();
    const Uint64 streamStart = SDL_GetTicks();
    std::cout << "SDL scene preview (" << window.driverName() << "): " << cubes.size()
              << " opaque Cube/MeshCube instances\n";
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            bool acceptCameraInput = !captureDir.has_value();
#ifdef RECUBIN_SDL_GPU_STUDIO
            if (studioUi) {
                studioUi->processEvent(event);
                acceptCameraInput = acceptCameraInput && !studioUi->wantsKeyboard();
            }
#endif
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                running = false;
            }
            if (acceptCameraInput && event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT) {
                    const float step = event.key.key == SDLK_LEFT ? -0.1f : 0.1f;
                    orbitAngle = std::remainder(orbitAngle + step, 2.0f * std::numbers::pi_v<float>);
                    cameraDirty = true;
                } else if (event.key.key == SDLK_HOME) {
                    orbitAngle = 0.0f;
                    cameraDirty = true;
                } else if (!terrains.empty()) {
                    switch (event.key.key) {
                    case SDLK_A: center.x -= chunkWidth; break;
                    case SDLK_D: center.x += chunkWidth; break;
                    case SDLK_W: center.z -= chunkWidth; break;
                    case SDLK_S: center.z += chunkWidth; break;
                    default: continue;
                    }
                    terrainCameraPending = false;
                    cameraDirty = true;
                }
            }
        }
        if (!running) {
            break;
        }
        drawItems.resize(staticDrawCount);
        bool terrainSettled = true;
        for (const auto& terrain : terrains) {
            if (!terrain->update(center, renderer, error)) {
                std::cerr << "SDL scene preview: Terrain '" << terrain->name() << "': " << error << '\n';
                return 1;
            }
            terrainSettled = terrain->isSettled() && terrainSettled;
        }
        if (captureDir && !terrainSettled) {
            constexpr Uint64 terrainTimeoutMs = 30000;
            if (SDL_GetTicks() - streamStart >= terrainTimeoutMs) {
                std::cerr << "SDL scene preview: timed out waiting for Terrain in " << scenePath << '\n';
                return 1;
            }
            SDL_Delay(1);
            continue;
        }
        if (terrainCameraPending && terrainSettled) {
            center.y = terrains.front()->surfaceAtOrigin();
            terrainCameraPending = false;
            cameraDirty = true;
        }
        auto frame = window.beginFrame(&error);
        if (!frame) {
            std::cerr << "SDL scene preview: frame acquisition failed: " << error << '\n';
            return 1;
        }
        const unsigned width = frame.width != 0 ? frame.width : PREVIEW_WIDTH;
        const unsigned height = frame.height != 0 ? frame.height : PREVIEW_HEIGHT;
        if (config.present && !frame.swapchain) {
            if (!window.cancel(frame, &error)) {
                std::cerr << "SDL scene preview: cannot cancel unavailable swapchain frame: " << error << '\n';
                return 1;
            }
            SDL_Delay(16);
            continue;
        }
        if (cameraDirty || width != previousWidth || height != previousHeight) {
            const float aspect = static_cast<float>(width) / static_cast<float>(height);
            const float distance = radius * std::max(1.0f, 1.0f / aspect);
            const float sine = std::sin(orbitAngle);
            const float cosine = std::cos(orbitAngle);
            const Vector3 eye = center +
                Vector3(1.35f * cosine + 1.65f * sine, 0.9f, 1.65f * cosine - 1.35f * sine) * distance;
            const Matrix4 view = Matrix4::LookAt(eye, center, Vector3(0, 1, 0));
            // The orbit preview frames the loaded scene, so center its shadow coverage there.
            lightProjection = shadowProjection(*workspaces.front(), center, Vector3{});
            const Matrix4 projection = sdlPerspective(60.0f, aspect, std::max(0.01f, distance * 0.01f),
                                                          distance * 8.0f);
            viewProjection = projection * view;
            for (std::size_t index = 0; index < drawItems.size(); ++index) {
                const Matrix4 mvp = viewProjection * drawModels[index];
                std::copy_n(mvp.m, 16, drawItems[index].modelViewProjection.begin());
                if (lightProjection) {
                    const Matrix4 shadowMvp = *lightProjection * drawModels[index];
                    std::copy_n(shadowMvp.m, 16, drawItems[index].shadowModelViewProjection.begin());
                }
            }
            previousWidth = width;
            previousHeight = height;
            cameraDirty = false;
        }
        for (const auto& terrain : terrains) {
            terrain->appendDraws(viewProjection, lightProjection, drawItems);
        }
        SDL_GPUTexture* resultTexture = nullptr;
        const float effectTime = captureDir ? 0.0f : static_cast<float>(SDL_GetTicks() - streamStart) / 1000.0f;
        if (!renderer.draw(*frame.command, width, height, drawItems, error, lightProjection.has_value()) ||
            !postEffects.apply(*workspaces.front(), *frame.command, *renderer.colorTarget(),
                               width, height, effectTime, resultTexture, error)) {
            std::cerr << "SDL scene preview: draw failed: " << error << '\n';
            if (!window.submit(frame, nullptr, &error)) {
                std::cerr << "SDL scene preview: cannot retire failed frame: " << error << '\n';
            }
            return 1;
        }
        bool captureReady = true;
#ifdef RECUBIN_SDL_GPU_STUDIO
        if (studioUi) {
            SDL_GPUTexture* destination = captureDir ? studioCaptureTarget.get() : frame.swapchain;
            if (!studioUi->draw(*frame.command, *destination, *resultTexture, error)) {
                std::cerr << "SDL scene preview: Studio UI draw failed: " << error << '\n';
                const bool retired = frame.swapchain ? window.submit(frame, nullptr, &error)
                                                    : window.cancel(frame, &error);
                if (!retired) {
                    std::cerr << "SDL scene preview: cannot retire failed UI frame: " << error << '\n';
                }
                return 1;
            }
            resultTexture = destination;
            captureReady = studioUi->hasSceneImage();
        } else
#endif
        if (frame.swapchain) {
            blitSdlSceneFrame(*frame.command, *resultTexture, *frame.swapchain, width, height);
        }

        if (!captureDir || !captureReady) {
            if (!window.submit(frame, nullptr, &error)) {
                std::cerr << "SDL scene preview: submit failed: " << error << '\n';
                return 1;
            }
            if (captureDir && ++layoutAttempts == 8) {
                std::cerr << "SDL scene preview: ImGui did not emit the Scene image for capture\n";
                return 1;
            }
            continue;
        }
        if (!captureSdlSceneFrame(window, frame, resultTexture, width, height, *captureDir, error)) {
            std::cerr << "SDL scene preview: capture failed: " << error << '\n';
            return 1;
        }
        running = false;
    }
#ifdef RECUBIN_SDL_GPU_STUDIO
    // Destroy the UI resources while the GPU device is still alive. The
    // capture texture has a device-bound RAII deleter and is released first.
    studioCaptureTarget.reset();
    studioUi.reset();
#endif
    return 0;
}
