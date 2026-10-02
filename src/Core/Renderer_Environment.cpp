#include <Core/Renderer.hpp>
#include <Core/FileLoader.hpp>
#include <Util/Logger.hpp>
#include <Instances/Skybox.hpp>
#include <Instances/Decal.hpp>
#include <include/Math/CFrame.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// ===================================================
//  PBR用の環境反射キューブマップ
//
//  Skyboxの6面テクスチャから、(1)Skybox自体を内側から6方向に描いたソース
//  キューブマップ、(2)GGX重要度サンプリングでroughnessごとにぼかしたプレフィルタ済み
//  キューブマップ(mipがroughness)を作る。computeシェーダー等のGL 4.2以降の機能は
//  使わず、フルスクリーンパスだけで完結する(OpenGL 4.1可)。
//  Skyboxが無い/テクスチャ未読込のときは、手続き的な空のグラデーションを使う。
// ===================================================
namespace {

constexpr int ENV_SOURCE_SIZE    = 256;  // Skyboxを描くソースキューブマップの一辺
constexpr int ENV_PREFILTER_SIZE = 128;  // プレフィルタ後のmip0の一辺
constexpr int ENV_PREFILTER_MIPS = 6;    // 128,64,32,16,8,4
constexpr int ENV_FALLBACK_SIZE  = 32;   // 手続き的な空。32→1の6段

struct CubeFaceView {
    Vector3 forward;
    Vector3 up;
};

// OpenGLのキューブマップ面(+X,-X,+Y,-Y,+Z,-Z)ごとの視線と上方向。この向きで90度FOVの
// 画像を描くと、方向でサンプルするキューブマップ規約と一致する。
const CubeFaceView kFaceViews[6] = {
    { Vector3( 1, 0, 0), Vector3(0, -1, 0) },
    { Vector3(-1, 0, 0), Vector3(0, -1, 0) },
    { Vector3( 0, 1, 0), Vector3(0, 0,  1) },
    { Vector3( 0,-1, 0), Vector3(0, 0, -1) },
    { Vector3( 0, 0, 1), Vector3(0, -1, 0) },
    { Vector3( 0, 0,-1), Vector3(0, -1, 0) },
};

// キューブ面のテクセル位置(u,v は -1..1、vは画像の行方向)から方向を求める。
// env_prefilter_fragment.glsl の faceDirection と同じ規約。
Vector3 faceDirection(int face, float u, float v) {
    switch (face) {
        case 0:  return Vector3( 1.0f,   -v,   -u);
        case 1:  return Vector3(-1.0f,   -v,    u);
        case 2:  return Vector3(    u, 1.0f,    v);
        case 3:  return Vector3(    u,-1.0f,   -v);
        case 4:  return Vector3(    u,   -v, 1.0f);
        default: return Vector3(   -u,   -v,-1.0f);
    }
}

std::size_t hashCombine(std::size_t seed, std::size_t value) {
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

// Skyboxの見た目を決める入力(各DecalのFace/TextureID/色)の識別値。0はテクスチャなし。
// 子の走査順に依存しないよう、Decalごとのハッシュは和で合成する。
std::size_t skyboxSignature(Skybox& skybox) {
    std::size_t sum = 0;
    bool anyTexture = false;
    for (const auto& [name, child] : skybox.getChildren()) {
        if (!child->IsA("Decal")) continue;
        const auto* decal = static_cast<const Decal*>(child.get());
        if (decal->TextureID == 0) continue;
        anyTexture = true;
        std::size_t h = hashCombine(0, static_cast<std::size_t>(decal->face));
        h = hashCombine(h, decal->TextureID);
        for (float channel : { decal->Color.r, decal->Color.g, decal->Color.b, decal->Color.a })
            h = hashCombine(h, static_cast<std::size_t>(std::lround(channel * 255.0f)));
        sum += h;
    }
    if (!anyTexture) return 0;
    return sum == 0 ? 1 : sum;
}

// 手続き的な空: 水平線から天頂へ青く、地平より下は暗い地面色。
void skyGradient(float y, float out[3]) {
    const float horizon[3] = { 0.80f, 0.84f, 0.90f };
    const float zenith[3]  = { 0.35f, 0.55f, 0.88f };
    const float ground[3]  = { 0.34f, 0.32f, 0.30f };
    for (int c = 0; c < 3; ++c) {
        if (y >= 0.0f) {
            out[c] = horizon[c] + (zenith[c] - horizon[c]) * std::sqrt(y);
        } else {
            const float t = std::min(-y / 0.35f, 1.0f);
            const float s = t * t * (3.0f - 2.0f * t);
            out[c] = horizon[c] + (ground[c] - horizon[c]) * s;
        }
    }
}

// 実行中のGL状態を保存し、スコープを抜けるときに復元する。環境マップの生成は
// メインパスの途中で走るため、呼び出し側のFBO・viewport・描画状態を壊さない。
class EnvBakeStateGuard {
public:
    EnvBakeStateGuard() {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_drawFbo);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &m_readFbo);
        glGetIntegerv(GL_VIEWPORT, m_viewport);
        glGetIntegerv(GL_CURRENT_PROGRAM, &m_program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &m_vao);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &m_activeTexture);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_texture2D);
        glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &m_textureCube);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &m_depthMask);
        m_depthTest = glIsEnabled(GL_DEPTH_TEST);
        m_blend = glIsEnabled(GL_BLEND);
        m_cull = glIsEnabled(GL_CULL_FACE);
    }

    ~EnvBakeStateGuard() {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(m_drawFbo));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(m_readFbo));
        glViewport(m_viewport[0], m_viewport[1], m_viewport[2], m_viewport[3]);
        glUseProgram(static_cast<GLuint>(m_program));
        glBindVertexArray(static_cast<GLuint>(m_vao));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(m_texture2D));
        glBindTexture(GL_TEXTURE_CUBE_MAP, static_cast<GLuint>(m_textureCube));
        glActiveTexture(static_cast<GLenum>(m_activeTexture));
        glDepthMask(m_depthMask);
        if (m_depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (m_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        if (m_cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    }

    EnvBakeStateGuard(const EnvBakeStateGuard&) = delete;
    EnvBakeStateGuard& operator=(const EnvBakeStateGuard&) = delete;

private:
    GLint m_drawFbo = 0, m_readFbo = 0, m_program = 0, m_vao = 0;
    GLint m_activeTexture = GL_TEXTURE0, m_texture2D = 0, m_textureCube = 0;
    GLint m_viewport[4] = {};
    GLboolean m_depthMask = GL_TRUE;
    bool m_depthTest = false, m_blend = false, m_cull = false;
};

void setCubemapSampling(GLuint texture, int levelCount) {
    glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                    levelCount > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, levelCount - 1);
}

// Skybox自身を、中心から6方向へ90度FOVで描いてソースキューブマップを作る。
// 面ごとのDecal/テクスチャ向き/色はSkybox::drawがそのまま解決する。
void renderSkyboxToCubemap(Renderer& renderer, Skybox& skybox, GLuint sourceTexture) {
    glBindTexture(GL_TEXTURE_CUBE_MAP, sourceTexture);
    for (int face = 0; face < 6; ++face) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA8,
                     ENV_SOURCE_SIZE, ENV_SOURCE_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    setCubemapSampling(sourceTexture, 1);

    glUseProgram(renderer.shaderProgram);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glViewport(0, 0, ENV_SOURCE_SIZE, ENV_SOURCE_SIZE);

    const Matrix4 projection = Matrix4::Perspective(90.0f, 1.0f, 0.1f, 10000.0f);
    const Vector3 size = skybox.Size;
    const Matrix4 model = CFrame(Vector3(0.0f, 0.0f, 0.0f), skybox.getWorldCFrame().Rotation).toMatrix4() *
                          Matrix4::Scale(size.x, size.y, size.z);

    // メインシェーダーのuniformを、Skyboxを単色(unlit)で描く状態に揃える。
    glUniformMatrix4fv(renderer.projectionLoc, 1, GL_FALSE, projection.m);
    glUniformMatrix4fv(renderer.modelLoc, 1, GL_FALSE, model.m);
    glUniform3f(renderer.viewPosLoc, 0.0f, 0.0f, 0.0f);
    if (renderer.unlitLoc != -1) glUniform1f(renderer.unlitLoc, 1.0f);
    if (renderer.triplanarLoc != -1) glUniform1f(renderer.triplanarLoc, 0.0f);
    if (renderer.texScaleLoc != -1) glUniform1f(renderer.texScaleLoc, 1.0f);
    if (renderer.useVertexColorLoc != -1) glUniform1f(renderer.useVertexColorLoc, 0.0f);
    if (renderer.uIsLiquidLoc != -1) glUniform1f(renderer.uIsLiquidLoc, 0.0f);
    if (renderer.m_uInstancedLoc != -1) glUniform1f(renderer.m_uInstancedLoc, 0.0f);
    if (renderer.m_uPbrMaterialLoc != -1) glUniform4f(renderer.m_uPbrMaterialLoc, 0.0f, 0.0f, 0.0f, 0.0f);
    if (renderer.surfaceMarkPassLoc != -1) glUniform1f(renderer.surfaceMarkPassLoc, 0.0f);
    glUniform1i(glGetUniformLocation(renderer.shaderProgram, "uDecalCount"), 0);
    glActiveTexture(GL_TEXTURE0);

    for (int face = 0; face < 6; ++face) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, sourceTexture, 0);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        const Matrix4 view = Matrix4::LookAt(Vector3(0.0f, 0.0f, 0.0f), kFaceViews[face].forward,
                                             kFaceViews[face].up);
        glUniformMatrix4fv(renderer.viewLoc, 1, GL_FALSE, view.m);
        skybox.draw(renderer.modelLoc, renderer.shaderProgram);
    }

    // プレフィルタが拡大/縮小サンプルするため、ソースにもmipを持たせる(256→1の9段)。
    glBindTexture(GL_TEXTURE_CUBE_MAP, sourceTexture);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, 8);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
}

// ソースキューブマップをroughnessごとにぼかし、mipレベルmへ roughness = m/(mipCount-1) を書く。
void prefilterCubemap(Renderer& renderer, GLuint sourceTexture, GLuint targetTexture) {
    glBindTexture(GL_TEXTURE_CUBE_MAP, targetTexture);
    for (int mip = 0; mip < ENV_PREFILTER_MIPS; ++mip) {
        const int size = ENV_PREFILTER_SIZE >> mip;
        for (int face = 0; face < 6; ++face) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, mip, GL_RGBA16F,
                         size, size, 0, GL_RGBA, GL_FLOAT, nullptr);
        }
    }
    setCubemapSampling(targetTexture, ENV_PREFILTER_MIPS);

    glUseProgram(renderer.m_envPrefilterShader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, sourceTexture);
    glUniform1i(glGetUniformLocation(renderer.m_envPrefilterShader, "uSrc"), 0);
    glUniform1f(glGetUniformLocation(renderer.m_envPrefilterShader, "uSrcSize"),
                static_cast<float>(ENV_SOURCE_SIZE));
    const GLint roughnessLoc = glGetUniformLocation(renderer.m_envPrefilterShader, "uRoughness");
    const GLint faceLoc = glGetUniformLocation(renderer.m_envPrefilterShader, "uFace");
    const GLint sizeLoc = glGetUniformLocation(renderer.m_envPrefilterShader, "uSize");

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(renderer.m_postVAO);
    for (int mip = 0; mip < ENV_PREFILTER_MIPS; ++mip) {
        const int size = ENV_PREFILTER_SIZE >> mip;
        glViewport(0, 0, size, size);
        glUniform1f(roughnessLoc, static_cast<float>(mip) / static_cast<float>(ENV_PREFILTER_MIPS - 1));
        glUniform1f(sizeLoc, static_cast<float>(size));
        for (int face = 0; face < 6; ++face) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, targetTexture, mip);
            glUniform1i(faceLoc, face);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
    }
}

} // namespace

void Renderer::initEnvironmentRenderer() {
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);  // 面の継ぎ目でフィルタが途切れないように

    const std::string vertexSource = FileLoader::readText("shaders/postprocess_vertex.glsl");
    const std::string fragmentSource = FileLoader::readText("shaders/env_prefilter_fragment.glsl");
    if (vertexSource.empty() || fragmentSource.empty()) {
        RCBN_ERROR("Renderer: env_prefilter shader sources are missing; Skybox reflections fall back to the procedural sky");
    } else {
        const char* vertexText = vertexSource.c_str();
        const char* fragmentText = fragmentSource.c_str();
        GLint ok = 0;
        char log[1024];

        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertexText, nullptr);
        glCompileShader(vertexShader);
        glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            glGetShaderInfoLog(vertexShader, sizeof(log), nullptr, log);
            RCBN_ERROR("ENV_PREFILTER_VERT: " << log);
        }

        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragmentText, nullptr);
        glCompileShader(fragmentShader);
        GLint fragmentOk = 0;
        glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &fragmentOk);
        if (!fragmentOk) {
            glGetShaderInfoLog(fragmentShader, sizeof(log), nullptr, log);
            RCBN_ERROR("ENV_PREFILTER_FRAG: " << log);
        }

        if (ok && fragmentOk) {
            m_envPrefilterShader = glCreateProgram();
            glAttachShader(m_envPrefilterShader, vertexShader);
            glAttachShader(m_envPrefilterShader, fragmentShader);
            glLinkProgram(m_envPrefilterShader);
            GLint linked = 0;
            glGetProgramiv(m_envPrefilterShader, GL_LINK_STATUS, &linked);
            if (!linked) {
                glGetProgramInfoLog(m_envPrefilterShader, sizeof(log), nullptr, log);
                RCBN_ERROR("ENV_PREFILTER_PROGRAM: " << log);
                glDeleteProgram(m_envPrefilterShader);
                m_envPrefilterShader = 0;
            }
        }
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
    }

    glGenFramebuffers(1, &m_envFbo);

    // 最初は手続き的な空で初期化しておく（Skyboxが無いシーンでもPBRの反射が成立する）。
    updateEnvironmentMap(nullptr);
}

void Renderer::destroyEnvironmentRenderer() {
    if (m_envSpecularTex) glDeleteTextures(1, &m_envSpecularTex);
    if (m_envFbo) glDeleteFramebuffers(1, &m_envFbo);
    if (m_envPrefilterShader) glDeleteProgram(m_envPrefilterShader);
    m_envSpecularTex = 0;
    m_envFbo = 0;
    m_envPrefilterShader = 0;
}

void Renderer::updateEnvironmentMap(Skybox* skybox) {
    const std::size_t signature = skybox ? skyboxSignature(*skybox) : 0;
    if (m_envBuilt && signature == m_envSignature) return;

    EnvBakeStateGuard stateGuard;
    // 失敗しても同じ入力で毎フレーム再試行しないよう、先に識別値を記録する。
    m_envSignature = signature;
    m_envBuilt = true;

    GLuint newTexture = 0;
    float newMaxLod = 0.0f;

    if (signature != 0 && skybox && m_envPrefilterShader && m_envFbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_envFbo);
        GLuint sourceTexture = 0;
        glGenTextures(1, &sourceTexture);
        glGenTextures(1, &newTexture);
        renderSkyboxToCubemap(*this, *skybox, sourceTexture);
        prefilterCubemap(*this, sourceTexture, newTexture);
        glDeleteTextures(1, &sourceTexture);
        newMaxLod = static_cast<float>(ENV_PREFILTER_MIPS - 1);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            RCBN_ERROR("Renderer: environment framebuffer is incomplete; using the procedural sky");
            glDeleteTextures(1, &newTexture);
            newTexture = 0;
        }
    }

    if (newTexture == 0) {
        // 手続き的な空のグラデーション。滑らかなのでGGXプレフィルタは行わず、mipで代用する。
        glGenTextures(1, &newTexture);
        glBindTexture(GL_TEXTURE_CUBE_MAP, newTexture);
        std::vector<unsigned char> pixels(static_cast<std::size_t>(ENV_FALLBACK_SIZE) * ENV_FALLBACK_SIZE * 4);
        for (int face = 0; face < 6; ++face) {
            for (int row = 0; row < ENV_FALLBACK_SIZE; ++row) {
                for (int col = 0; col < ENV_FALLBACK_SIZE; ++col) {
                    const float u = (static_cast<float>(col) + 0.5f) / ENV_FALLBACK_SIZE * 2.0f - 1.0f;
                    const float v = (static_cast<float>(row) + 0.5f) / ENV_FALLBACK_SIZE * 2.0f - 1.0f;
                    const Vector3 direction = faceDirection(face, u, v);
                    float rgb[3];
                    skyGradient(direction.y / direction.length(), rgb);
                    unsigned char* texel = &pixels[(static_cast<std::size_t>(row) * ENV_FALLBACK_SIZE + col) * 4];
                    for (int c = 0; c < 3; ++c)
                        texel[c] = static_cast<unsigned char>(std::lround(std::min(std::max(rgb[c], 0.0f), 1.0f) * 255.0f));
                    texel[3] = 255;
                }
            }
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA8,
                         ENV_FALLBACK_SIZE, ENV_FALLBACK_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        }
        setCubemapSampling(newTexture, 6);  // 32→1の6段
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        newMaxLod = std::floor(std::log2(static_cast<float>(ENV_FALLBACK_SIZE)));
    }

    if (m_envSpecularTex) glDeleteTextures(1, &m_envSpecularTex);
    m_envSpecularTex = newTexture;
    m_envMaxLod = newMaxLod;
}

void Renderer::bindEnvironmentMap() {
    glActiveTexture(GL_TEXTURE0 + ENV_SPECULAR_UNIT);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_envSpecularTex);
    glActiveTexture(GL_TEXTURE0);
    if (m_uEnvMaxLodLoc != -1) glUniform1f(m_uEnvMaxLodLoc, m_envMaxLod);
}
