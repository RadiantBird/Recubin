#include <Core/Renderer.hpp>

#include <Instances/CelestialBody.hpp>
#include <Instances/Sun.hpp>
#include <Instances/Moon.hpp>
#include <Instances/Sphere.hpp>
#include <Util/GLUniformCache.hpp>
#include <Util/Logger.hpp>
#include <sokol/sokol_gfx.h>
#include <shaders/sky_flare.h>
#include <include/Math/CFrame.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace {

constexpr float DEGREES_TO_RADIANS = 3.14159265358979f / 180.0f;

// 天体を隠す物体とみなす深度のしきい値を、円盤の最前面より何倍手前(円盤半径の倍数)に置くか。
// 円盤自身（深度に書き込まれる球）が自分を遮らないための余裕。
constexpr float DISC_CLEARANCE = 1.15f;

// 手前の物体の上へグローがにじむ強さ。天体が見えている割合の分だけ効く（隠れていれば0）。
constexpr float GLOW_BLEED = 0.35f;

// Moonは最初のSunの反対側。Sunが無ければ既定のSun角の反対側に置く。
Vector3 moonDirectionFor(const Sun* primarySun) {
    const Vector2 reference = primarySun ? primarySun->Angle : Sun::defaultAngle();
    return -Sun::directionFromAngle(reference);
}

// 天体の方向（単位ベクトル）。Sunは自分のAngle、Moonは moonDirection。それ以外は描かない。
bool skyBodyDirection(CelestialBody& body, const Vector3& moonDirection, Vector3& direction) {
    if (body.IsA("Sun")) {
        direction = Sun::directionFromAngle(static_cast<Sun&>(body).Angle);
        return true;
    }
    if (body.IsA("Moon")) {
        direction = moonDirection;
        return true;
    }
    return false;
}

float finiteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

// 地平線の下へ沈むにつれて効果を消す係数。dir.y は高度の正弦。
float horizonFadeFactor(const CelestialBody& body, const Vector3& direction) {
    if (!body.HorizonFade) return 1.0f;
    const float t = std::clamp((direction.y + 0.05f) / 0.15f, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// 満ち欠けのある月か（満月以外は、発光球ではなく sky_flare の円盤で描く）。
bool isPhasedMoon(CelestialBody& body) {
    return body.IsA("Moon") && static_cast<Moon&>(body).hasPhase();
}

// sky_flare シェーダーで描く1枚のビルボード（グロー + 光条）。
struct FlareBillboard {
    Vector3 center;           // ワールド座標の中心
    float halfSize = 0.0f;    // 半幅(stud)
    float extent = 1.0f;      // 半幅が円盤半径の何倍か（シェーダー側の距離の単位）
    float glowIntensity = 0.0f;
    float glowRadius = 1.0f;
    float spikeIntensity = 0.0f;
    float spikeLength = 1.0f;
    float spikeRotation = 0.0f;  // ラジアン
    int spikeCount = 4;
};

void setVec3(float (&dst)[3], const Vector3& v) {
    dst[0] = v.x;
    dst[1] = v.y;
    dst[2] = v.z;
}

// sky_flare の 1 ドロー（postprocess 用 VAO のクアッド）。両ステージのユニフォームを送ってから描く。
void drawSkyFlareQuad(const ShdcProgram& program, const sky_flare_vs_params_t& vs, const sky_flare_fs_params_t& fs) {
    program.applyUniforms(UB_sky_flare_vs_params, vs);
    program.applyUniforms(UB_sky_flare_fs_params, fs);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void drawFlareBillboard(const ShdcProgram& program, sky_flare_vs_params_t& vs, sky_flare_fs_params_t& fs,
                        const FlareBillboard& b) {
    vs.uFullscreen = 0.0f;
    fs.uFullscreen = 0.0f;
    fs.uPhaseDisc = 0.0f;
    setVec3(vs.uCenter, b.center);
    vs.uHalfSize = b.halfSize;
    fs.uExtent = b.extent;
    fs.uGlowIntensity = b.glowIntensity;
    fs.uGlowRadius = b.glowRadius;
    fs.uSpikeIntensity = b.spikeIntensity;
    fs.uSpikeLength = b.spikeLength;
    fs.uSpikeRotation = b.spikeRotation;
    fs.uSpikeCount = static_cast<float>(b.spikeCount);
    drawSkyFlareQuad(program, vs, fs);
}

} // namespace

// ===================================================
//  太陽と月（空の円盤）
//
//  Sun/Moon は BaseCube ではなく軽量Instanceなので、ここでカメラ基準に直接描く。
//  位置は常にカメラから SKY_RENDER_DISTANCE の方向、見かけの大きさは Distance で決まる
//  （apparentDiameter）。共有のSphereジオメトリを、メインシェーダーのunlitで1ドローする。
//  満ち欠けのある月（Phase が満月以外）は、ここでは描かず renderSkyFlares の円盤で描く。
// ===================================================
void Renderer::renderCelestialBodies(Workspace& workspace, const Vector3& cameraPosition,
                                     const Sun* primarySun) {
    const auto& bodies = workspace.getRenderCelestialBodies();
    if (bodies.empty()) return;

    // Sphereジオメトリは最初のSphere生成時に作られるため、Sphereの無いシーンでは未生成。
    if (Sphere::s_VAO == 0) Sphere::initGeometry();
    if (Sphere::s_VAO == 0 || Sphere::s_IndexCount <= 0) return;

    const Vector3 moonDirection = moonDirectionFor(primarySun);

    // メインシェーダーを、単色(unlit)の個別描画と同じ状態にそろえる。
    m_main.setUnlit(1.0f);
    m_main.setTriplanar(0.0f);
    m_main.setTextureScale(1.0f);
    m_main.setUseVertexColor(0.0f);
    m_main.setIsLiquid(0.0f);
    m_main.setInstanced(0.0f);
    m_main.setPbrMaterial(0.0f, 0.0f, 0.0f, 0.0f);
    m_main.setUvScale(1.0f, 1.0f);
    m_main.setSurfaceGui(0.0f);
    m_main.setTextureTintColor(Color4(1.0f, 1.0f, 1.0f, 1.0f));
    m_main.setUseTextureTint(0.0f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, whiteTexture.glName());
    glBindVertexArray(Sphere::s_VAO);

    for (CelestialBody* body : bodies) {
        if (!body || body->Color.a <= 0.001f) continue;

        Vector3 direction;
        if (!skyBodyDirection(*body, moonDirection, direction)) continue;

        // 満ち欠けのある月は sky_flare の円盤で描く。シェーダーが無い環境では球で代用し、月を消さない。
        if (m_skyFlareProgram.isValid() && isPhasedMoon(*body)) continue;

        const float diameter = body->apparentDiameter();
        const Matrix4 model =
            CFrame(cameraPosition + direction * CelestialBody::SKY_RENDER_DISTANCE).toMatrix4() *
            Matrix4::Scale(diameter, diameter, diameter);
        m_main.setModel(model);
        m_main.setColor(Color4(body->Color.r, body->Color.g, body->Color.b, body->Color.a));

        // 半透明の円盤は深度を書かない（後ろの物を隠さない）。ブレンドはメインパス既定のまま。
        const bool translucent = body->Color.a < 0.999f;
        if (translucent) glDepthMask(GL_FALSE);
        m_main.flush();
        glDrawElements(GL_TRIANGLES, Sphere::s_IndexCount, GL_UNSIGNED_INT, nullptr);
        if (translucent) glDepthMask(GL_TRUE);
    }

    glBindVertexArray(VAO);  // 以降のメインパスは共有の立方体VAO前提
}

// ===================================================
//  太陽・月のオーバーレイ（逆光・光条・月の満ち欠け）
//
//  sky_flare シェーダーで、(1)天体の方向に置いたカメラ向きビルボードにグローと光条（加算）、
//  (2)画面全体にベール（加算）、(3)満ち欠けのある月の円盤（アルファブレンド）、を重ねる。
//  遮蔽はビューポートの深度のコピー(m_skyDepthTex)をシェーダーで読んで判定する:
//    光条・月の円盤: 手前の物体に隠れた画素は描かない
//    グロー        : 物体の上へは「天体が見えている割合」× GLOW_BLEND だけにじむ
//    ベール        : 天体が隠れているほど弱い
//  深度のしきい値は「天体の円盤の最前面より手前」なので、円盤自身や遠景のSkyboxは遮らない
//  （天体より遠い物体が遮らないのは円盤と同じ制約）。強さが0の効果は描かない。
// ===================================================
void Renderer::initSkyFlareRenderer() {
    if (!m_skyFlareProgram.create(*sky_flare_shader_desc(SG_BACKEND_GLCORE), "sky_flare")) {
        RCBN_ERROR("Renderer: sky_flare shader is unavailable; sun/moon glow, veil, spikes and moon phases are disabled");
    }
}

void Renderer::destroySkyFlareRenderer() {
    m_skyFlareProgram.destroy();
    if (m_skyDepthFbo) glDeleteFramebuffers(1, &m_skyDepthFbo);
    GfxTextures::destroy(m_skyDepthTex);
    m_skyDepthFbo = 0;
}

bool Renderer::updateSkySceneDepth(unsigned int sourceFbo, int width, int height) {
    if (m_skyDepthBlitFailed || width <= 0 || height <= 0) return false;

    if (!m_skyDepthTex || width != m_skyDepthWidth || height != m_skyDepthHeight) {
        // ブリット元（シーンの深度）と同じ形式で作る。深度の値そのものを読む設定は GfxTextures が入れる。
        GfxTextures::destroy(m_skyDepthTex);
        m_skyDepthTex = GfxTextures::createSceneDepthTarget(width, height, "sky scene depth");

        if (!m_skyDepthFbo) glGenFramebuffers(1, &m_skyDepthFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_skyDepthFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_skyDepthTex.glName(), 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, sourceFbo);
        if (!complete) {
            m_skyDepthBlitFailed = true;
            RCBN_WARN("Renderer: sky depth framebuffer is incomplete; sun/moon effects are drawn without occlusion");
            return false;
        }
        m_skyDepthWidth = width;
        m_skyDepthHeight = height;
    }

    // 深度のフォーマットが合わない環境ではブリットがGLエラーになる。その場合は遮蔽なしに切り替える。
    while (glGetError() != GL_NO_ERROR) {}
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_skyDepthFbo);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, sourceFbo);
    if (glGetError() != GL_NO_ERROR) {
        m_skyDepthBlitFailed = true;
        RCBN_WARN("Renderer: copying the viewport depth failed; sun/moon effects are drawn without occlusion");
        return false;
    }
    return true;
}

void Renderer::renderSkyFlares(Workspace& workspace, const ViewportRenderDesc& desc,
                               const Matrix4& view, const Matrix4& projection,
                               float fovYDegrees, const Sun* primarySun) {
    if (!m_skyFlareProgram.isValid() || !m_postVAO || desc.width <= 0 || desc.height <= 0) return;
    const auto& bodies = workspace.getRenderCelestialBodies();
    if (bodies.empty()) return;

    // 描くものが無ければGL状態に触れずに戻る（逆光/光条のいずれか、または満ち欠けのある月）。
    bool anyOverlay = false;
    for (CelestialBody* body : bodies) {
        if (body && body->Color.a > 0.001f && (body->hasFlareEffect() || isPhasedMoon(*body))) {
            anyOverlay = true;
            break;
        }
    }
    if (!anyOverlay) return;

    const Vector3 forward = desc.cameraForward.normalize();
    const Vector3 right = Vector3::Cross(forward, desc.cameraUp).normalize();
    const Vector3 up = Vector3::Cross(right, forward).normalize();
    const Vector3 moonDirection = moonDirectionFor(primarySun);

    // 遮蔽判定用に、ここまでに描いた不透明物の深度をコピーする（描画先FBOはそのまま）。
    const bool occlusionAvailable = updateSkySceneDepth(desc.fbo, desc.width, desc.height);

    // 呼び出し元の状態を保存し、深度なし・深度は書かずに描いたあとに復元する。
    // 深度テクスチャは頂点用・フラグメント用の 2 ユニット（0 と 1）に束縛する。
    GLint previousProgram = 0, previousVao = 0, previousActiveTexture = GL_TEXTURE0;
    GLint previousTextures[2] = {};
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    for (int unit = 0; unit < 2; ++unit) {
        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextures[unit]);
    }
    const GLboolean depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    GLboolean depthMaskWas = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMaskWas);

    m_skyFlareProgram.use();
    glBindVertexArray(m_postVAO);
    // 遮蔽はシェーダーが深度テクスチャで判定するので、ハードウェアの深度テストは使わない。
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);

    // ユニフォームは描画ごとに両ブロックを丸ごと送る。ここで共通の値を入れ、以降は変わる値だけ書き換える。
    sky_flare_vs_params_t vs = {};
    sky_flare_fs_params_t fs = {};
    const float aspect = static_cast<float>(desc.width) / static_cast<float>(desc.height);
    std::memcpy(vs.view, view.m, sizeof(vs.view));
    std::memcpy(vs.projection, projection.m, sizeof(vs.projection));
    setVec3(vs.uRight, right);
    setVec3(vs.uUp, up);
    setVec3(vs.uForward, forward);
    vs.uTanHalfFov = std::tan(fovYDegrees * 0.5f * DEGREES_TO_RADIANS);
    vs.uAspect = aspect;

    // 遮蔽判定の共通設定。投影行列の m[10]/m[14] で、深度バッファの値を視線方向の距離へ戻す。
    const GLuint sceneDepth = occlusionAvailable ? m_skyDepthTex.glName() : 0;
    m_skyFlareProgram.bindTexture(VIEW_sky_uSceneDepthVs, GL_TEXTURE_2D, sceneDepth);
    m_skyFlareProgram.bindTexture(VIEW_sky_uSceneDepth, GL_TEXTURE_2D, sceneDepth);
    vs.uOcclusionEnabled = fs.uOcclusionEnabled = occlusionAvailable ? 1.0f : 0.0f;
    vs.uProjA = fs.uProjA = projection.m[10];
    vs.uProjB = fs.uProjB = projection.m[14];
    fs.uViewportSize[0] = static_cast<float>(desc.width);
    fs.uViewportSize[1] = static_cast<float>(desc.height);
    fs.uBleed = GLOW_BLEED;

    for (CelestialBody* body : bodies) {
        if (!body || body->Color.a <= 0.001f) continue;
        const bool phased = isPhasedMoon(*body);
        if (!body->hasFlareEffect() && !phased) continue;

        Vector3 direction;
        if (!skyBodyDirection(*body, moonDirection, direction)) continue;

        const float discRadius = body->apparentDiameter() * 0.5f;
        const float facing = Vector3::Dot(forward, direction);
        const Vector3 center = desc.cameraPosition + direction * CelestialBody::SKY_RENDER_DISTANCE;

        // この天体を手前の物体が隠しているかの判定: 円盤の最前面より手前(視線方向の距離)にある物体。
        fs.uColor[0] = body->Color.r;
        fs.uColor[1] = body->Color.g;
        fs.uColor[2] = body->Color.b;
        setVec3(vs.uCenter, center);
        vs.uDiscRadius = discRadius;
        vs.uOccluderDepth = fs.uOccluderDepth =
            facing * CelestialBody::SKY_RENDER_DISTANCE - DISC_CLEARANCE * discRadius;

        // (1) 満ち欠けのある月の円盤（アルファブレンド）。影の部分は Earthshine だけ透けて空が見える。
        if (phased) {
            const Moon& moon = static_cast<const Moon&>(*body);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            vs.uFullscreen = fs.uFullscreen = 0.0f;
            fs.uPhaseDisc = 1.0f;
            fs.uStrength = body->Color.a;
            vs.uHalfSize = discRadius;
            fs.uExtent = 1.0f;
            fs.uPhase = moon.wrappedPhase();
            fs.uPhaseRotation = finiteOr(moon.PhaseRotation, 0.0f) * DEGREES_TO_RADIANS;
            fs.uEarthshine = std::clamp(finiteOr(moon.Earthshine, 0.0f), 0.0f, 1.0f);
            drawSkyFlareQuad(m_skyFlareProgram, vs, fs);
            fs.uPhaseDisc = 0.0f;
        }

        if (!body->hasFlareEffect()) continue;
        const float strength = body->Color.a * horizonFadeFactor(*body, direction);
        if (strength <= 0.001f) continue;
        fs.uStrength = strength;
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // アルファは常に1なので、出力した光をそのまま足す

        // 範囲外・非有限の値は描画側で丸める（シェーダーに不正値を渡さない）。
        const float glowIntensity = std::max(finiteOr(body->GlowIntensity, 0.0f), 0.0f);
        const float glowRadius = std::clamp(finiteOr(body->GlowRadius, 6.0f), 1.0f, 60.0f);
        const float spikeIntensity = std::max(finiteOr(body->SpikeIntensity, 0.0f), 0.0f);
        const float spikeLength = std::clamp(finiteOr(body->SpikeLength, 12.0f), 1.0f, 60.0f);
        const int spikeCount = std::clamp(body->SpikeCount, 2, 16);
        const float spikeRotation = finiteOr(body->SpikeRotation, 0.0f) * DEGREES_TO_RADIANS;
        const float veilIntensity = std::max(finiteOr(body->VeilIntensity, 0.0f), 0.0f);
        const float veilFalloff = std::clamp(finiteOr(body->VeilFalloff, 6.0f), 1.0f, 64.0f);

        // (2) グロー + 光条: 天体の方向に置いた、円盤半径の extent 倍の大きさのビルボード。
        if (glowIntensity > 0.0f || spikeIntensity > 0.0f) {
            FlareBillboard billboard;
            billboard.center = center;
            billboard.extent = std::max(glowIntensity > 0.0f ? glowRadius : 1.0f,
                                        spikeIntensity > 0.0f ? spikeLength : 1.0f);
            billboard.halfSize = discRadius * billboard.extent;
            billboard.glowIntensity = glowIntensity;
            billboard.glowRadius = glowRadius;
            billboard.spikeIntensity = spikeIntensity;
            billboard.spikeLength = spikeLength;
            billboard.spikeRotation = spikeRotation;
            billboard.spikeCount = spikeCount;
            drawFlareBillboard(m_skyFlareProgram, vs, fs, billboard);
        }

        // (3) ベール: カメラが天体を向くほど画面全体が白く霞む。天体が隠れているほど弱い。
        //     天体から大きく外れた向きでは描かない。
        if (veilIntensity > 0.0f && facing > -0.2f) {
            vs.uFullscreen = fs.uFullscreen = 1.0f;
            fs.uVeilIntensity = veilIntensity;
            fs.uVeilFalloff = veilFalloff;
            setVec3(fs.uBodyDirection, direction);
            drawSkyFlareQuad(m_skyFlareProgram, vs, fs);
            vs.uFullscreen = fs.uFullscreen = 0.0f;
        }
    }

    // 呼び出し元（パーティクル等と同じ前提）の状態へ戻す。
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (!blendWasEnabled) glDisable(GL_BLEND);
    if (depthTestWasEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthMask(depthMaskWas);
    for (int unit = 0; unit < 2; ++unit) {
        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTextures[unit]));
    }
    glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    glBindVertexArray(static_cast<GLuint>(previousVao));
    glUseProgram(static_cast<GLuint>(previousProgram));
}
