#include <Core/Renderer.hpp>
#include <Core/FileLoader.hpp>
#include <Instances/CelestialBody.hpp>
#include <Instances/Sun.hpp>
#include <Instances/Moon.hpp>
#include <Instances/Sphere.hpp>
#include <Util/GLUniformCache.hpp>
#include <Util/Logger.hpp>
#include <include/Math/CFrame.hpp>
#include <algorithm>
#include <cmath>
#include <string>

namespace {

constexpr float DEGREES_TO_RADIANS = 3.14159265358979f / 180.0f;

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

// 光条ビルボードを円盤の最前面より何倍手前（円盤半径の倍数）に置くか。円盤は深度に書き込まれるため、
// 同じ距離に置くと光条が円盤自身に隠れてしまう。
constexpr float SPIKE_DISC_CLEARANCE = 1.15f;
// 光条ビルボードの最短距離(stud)。天体がカメラの真横付近にあるときに距離が0以下にならないようにする。
constexpr float SPIKE_MIN_DISTANCE = 100.0f;

// sky_flare シェーダーで描く1枚のビルボード（グローまたは光条）。
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

void drawFlareBillboard(const Renderer::SkyFlareUniforms& u, const FlareBillboard& b) {
    glUniform1f(u.fullscreen, 0.0f);
    glUniform3f(u.center, b.center.x, b.center.y, b.center.z);
    glUniform1f(u.halfSize, b.halfSize);
    glUniform1f(u.extent, b.extent);
    glUniform1f(u.glowIntensity, b.glowIntensity);
    glUniform1f(u.glowRadius, b.glowRadius);
    glUniform1f(u.spikeIntensity, b.spikeIntensity);
    glUniform1f(u.spikeLength, b.spikeLength);
    glUniform1f(u.spikeRotation, b.spikeRotation);
    glUniform1i(u.spikeCount, b.spikeCount);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

} // namespace

// ===================================================
//  太陽と月（空の円盤）
//
//  Sun/Moon は BaseCube ではなく軽量Instanceなので、ここでカメラ基準に直接描く。
//  位置は常にカメラから SKY_RENDER_DISTANCE の方向、見かけの大きさは Distance で決まる
//  （apparentDiameter）。共有のSphereジオメトリを、メインシェーダーのunlitで1ドローする。
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
    static CachedUniform s_uvScaleCache;
    static CachedUniform s_isSurfaceGuiCache;
    static CachedUniform s_tintColorCache;
    static CachedUniform s_useTintCache;
    const int uvScaleLoc = cachedUniformLocation(shaderProgram, s_uvScaleCache, "uvScale");
    const int isSurfaceGuiLoc = cachedUniformLocation(shaderProgram, s_isSurfaceGuiCache, "isSurfaceGui");
    const int tintColorLoc = cachedUniformLocation(shaderProgram, s_tintColorCache, "uTextureTintColor");
    const int useTintLoc = cachedUniformLocation(shaderProgram, s_useTintCache, "uUseTextureTint");
    if (unlitLoc != -1) glUniform1f(unlitLoc, 1.0f);
    if (triplanarLoc != -1) glUniform1f(triplanarLoc, 0.0f);
    if (texScaleLoc != -1) glUniform1f(texScaleLoc, 1.0f);
    if (useVertexColorLoc != -1) glUniform1f(useVertexColorLoc, 0.0f);
    if (uIsLiquidLoc != -1) glUniform1f(uIsLiquidLoc, 0.0f);
    if (m_uInstancedLoc != -1) glUniform1f(m_uInstancedLoc, 0.0f);
    if (m_uPbrMaterialLoc != -1) glUniform4f(m_uPbrMaterialLoc, 0.0f, 0.0f, 0.0f, 0.0f);
    if (uvScaleLoc != -1) glUniform2f(uvScaleLoc, 1.0f, 1.0f);
    if (isSurfaceGuiLoc != -1) glUniform1f(isSurfaceGuiLoc, 0.0f);
    if (tintColorLoc != -1) glUniform4f(tintColorLoc, 1.0f, 1.0f, 1.0f, 1.0f);
    if (useTintLoc != -1) glUniform1f(useTintLoc, 0.0f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, whiteTexture);
    glBindVertexArray(Sphere::s_VAO);

    for (CelestialBody* body : bodies) {
        if (!body || body->Color.a <= 0.001f) continue;

        Vector3 direction;
        if (!skyBodyDirection(*body, moonDirection, direction)) continue;

        const float diameter = body->apparentDiameter();
        const Matrix4 model =
            CFrame(cameraPosition + direction * CelestialBody::SKY_RENDER_DISTANCE).toMatrix4() *
            Matrix4::Scale(diameter, diameter, diameter);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, model.m);
        if (ourColorLoc != -1) {
            glUniform4f(ourColorLoc, body->Color.r, body->Color.g, body->Color.b, body->Color.a);
        }

        // 半透明の円盤は深度を書かない（後ろの物を隠さない）。ブレンドはメインパス既定のまま。
        const bool translucent = body->Color.a < 0.999f;
        if (translucent) glDepthMask(GL_FALSE);
        glDrawElements(GL_TRIANGLES, Sphere::s_IndexCount, GL_UNSIGNED_INT, nullptr);
        if (translucent) glDepthMask(GL_TRUE);
    }

    glBindVertexArray(VAO);  // 以降のメインパスは共有の立方体VAO前提
}

// ===================================================
//  太陽・月の逆光（グロー＋ベール）と光条（回折スパイク）
//
//  sky_flare シェーダーで、(1)天体の方向に置いたカメラ向きビルボードにグローと光条、
//  (2)画面全体にベール、を加算で重ねる。深度テストはしない（手前の物体の上にも被さり、
//  縁のにじみも出る）。強さが0の効果は描かない。
// ===================================================
void Renderer::initSkyFlareRenderer() {
    const std::string vertexSource = FileLoader::readText("shaders/sky_flare_vertex.glsl");
    const std::string fragmentSource = FileLoader::readText("shaders/sky_flare_fragment.glsl");
    if (vertexSource.empty() || fragmentSource.empty()) {
        RCBN_ERROR("Renderer: sky_flare shader sources are missing; sun/moon glow, veil and spikes are disabled");
        return;
    }

    char log[1024];
    GLint ok = 0;
    const char* vertexText = vertexSource.c_str();
    const char* fragmentText = fragmentSource.c_str();

    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexText, nullptr);
    glCompileShader(vertexShader);
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glGetShaderInfoLog(vertexShader, sizeof(log), nullptr, log);
        RCBN_ERROR("SKY_FLARE_VERT: " << log);
    }
    const GLint vertexOk = ok;

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentText, nullptr);
    glCompileShader(fragmentShader);
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glGetShaderInfoLog(fragmentShader, sizeof(log), nullptr, log);
        RCBN_ERROR("SKY_FLARE_FRAG: " << log);
    }
    const GLint fragmentOk = ok;

    if (vertexOk && fragmentOk) {
        m_skyFlareShader = glCreateProgram();
        glAttachShader(m_skyFlareShader, vertexShader);
        glAttachShader(m_skyFlareShader, fragmentShader);
        glLinkProgram(m_skyFlareShader);
        GLint linked = 0;
        glGetProgramiv(m_skyFlareShader, GL_LINK_STATUS, &linked);
        if (!linked) {
            glGetProgramInfoLog(m_skyFlareShader, sizeof(log), nullptr, log);
            RCBN_ERROR("SKY_FLARE_PROGRAM: " << log);
            glDeleteProgram(m_skyFlareShader);
            m_skyFlareShader = 0;
        }
    }
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    if (!m_skyFlareShader) return;

    const auto loc = [this](const char* name) { return glGetUniformLocation(m_skyFlareShader, name); };
    m_skyFlareLoc.fullscreen = loc("uFullscreen");
    m_skyFlareLoc.view = loc("view");
    m_skyFlareLoc.projection = loc("projection");
    m_skyFlareLoc.center = loc("uCenter");
    m_skyFlareLoc.right = loc("uRight");
    m_skyFlareLoc.up = loc("uUp");
    m_skyFlareLoc.halfSize = loc("uHalfSize");
    m_skyFlareLoc.forward = loc("uForward");
    m_skyFlareLoc.tanHalfFov = loc("uTanHalfFov");
    m_skyFlareLoc.aspect = loc("uAspect");
    m_skyFlareLoc.color = loc("uColor");
    m_skyFlareLoc.strength = loc("uStrength");
    m_skyFlareLoc.extent = loc("uExtent");
    m_skyFlareLoc.glowIntensity = loc("uGlowIntensity");
    m_skyFlareLoc.glowRadius = loc("uGlowRadius");
    m_skyFlareLoc.spikeIntensity = loc("uSpikeIntensity");
    m_skyFlareLoc.spikeLength = loc("uSpikeLength");
    m_skyFlareLoc.spikeRotation = loc("uSpikeRotation");
    m_skyFlareLoc.spikeCount = loc("uSpikeCount");
    m_skyFlareLoc.veilIntensity = loc("uVeilIntensity");
    m_skyFlareLoc.veilFalloff = loc("uVeilFalloff");
    m_skyFlareLoc.bodyDirection = loc("uBodyDirection");
}

void Renderer::destroySkyFlareRenderer() {
    if (m_skyFlareShader) glDeleteProgram(m_skyFlareShader);
    m_skyFlareShader = 0;
}

void Renderer::renderSkyFlares(Workspace& workspace, const ViewportRenderDesc& desc,
                               const Matrix4& view, const Matrix4& projection,
                               float fovYDegrees, const Sun* primarySun) {
    if (!m_skyFlareShader || !m_postVAO || desc.height <= 0) return;
    const auto& bodies = workspace.getRenderCelestialBodies();
    if (bodies.empty()) return;

    const Vector3 forward = desc.cameraForward.normalize();
    const Vector3 right = Vector3::Cross(forward, desc.cameraUp).normalize();
    const Vector3 up = Vector3::Cross(right, forward).normalize();
    const Vector3 moonDirection = moonDirectionFor(primarySun);

    // 効果のある天体が無ければGL状態に触れずに戻る。
    bool anyEffect = false;
    for (CelestialBody* body : bodies) {
        if (body && body->hasFlareEffect() && body->Color.a > 0.001f) { anyEffect = true; break; }
    }
    if (!anyEffect) return;

    // 呼び出し元の状態を保存し、加算ブレンド・深度なしで描いたあとに復元する。
    GLint previousProgram = 0, previousVao = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    const GLboolean depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    GLboolean depthMaskWas = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMaskWas);
    GLint depthFuncWas = GL_LESS;
    glGetIntegerv(GL_DEPTH_FUNC, &depthFuncWas);

    glUseProgram(m_skyFlareShader);
    glBindVertexArray(m_postVAO);
    // 深度テストはパスごとに切り替える（グロー/ベール: なし、光条: あり）。どのパスも深度は書かない。
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // アルファは常に1なので、出力した光をそのまま足す

    const SkyFlareUniforms& u = m_skyFlareLoc;
    const float aspect = static_cast<float>(desc.width) / static_cast<float>(desc.height);
    glUniformMatrix4fv(u.view, 1, GL_FALSE, view.m);
    glUniformMatrix4fv(u.projection, 1, GL_FALSE, projection.m);
    glUniform3f(u.right, right.x, right.y, right.z);
    glUniform3f(u.up, up.x, up.y, up.z);
    glUniform3f(u.forward, forward.x, forward.y, forward.z);
    glUniform1f(u.tanHalfFov, std::tan(fovYDegrees * 0.5f * DEGREES_TO_RADIANS));
    glUniform1f(u.aspect, aspect);

    for (CelestialBody* body : bodies) {
        if (!body || !body->hasFlareEffect() || body->Color.a <= 0.001f) continue;

        Vector3 direction;
        if (!skyBodyDirection(*body, moonDirection, direction)) continue;

        const float strength = body->Color.a * horizonFadeFactor(*body, direction);
        if (strength <= 0.001f) continue;

        glUniform3f(u.color, body->Color.r, body->Color.g, body->Color.b);
        glUniform1f(u.strength, strength);

        // 範囲外・非有限の値は描画側で丸める（シェーダーに不正値を渡さない）。
        const float glowIntensity = std::max(finiteOr(body->GlowIntensity, 0.0f), 0.0f);
        const float glowRadius = std::clamp(finiteOr(body->GlowRadius, 6.0f), 1.0f, 60.0f);
        const float spikeIntensity = std::max(finiteOr(body->SpikeIntensity, 0.0f), 0.0f);
        const float spikeLength = std::clamp(finiteOr(body->SpikeLength, 12.0f), 1.0f, 60.0f);
        const int spikeCount = std::clamp(body->SpikeCount, 2, 16);
        const float spikeRotation = finiteOr(body->SpikeRotation, 0.0f) * DEGREES_TO_RADIANS;
        const float veilIntensity = std::max(finiteOr(body->VeilIntensity, 0.0f), 0.0f);
        const float veilFalloff = std::clamp(finiteOr(body->VeilFalloff, 6.0f), 1.0f, 64.0f);

        const float discRadius = body->apparentDiameter() * 0.5f;
        const float facing = Vector3::Dot(forward, direction);

        // (1) グロー: 天体の方向に置いた、円盤半径の glowRadius 倍の大きさのビルボード。
        //     深度テストなし（手前の物体の上にも被さり、縁のにじみも出る）。
        if (glowIntensity > 0.0f) {
            glDisable(GL_DEPTH_TEST);
            FlareBillboard glow;
            glow.center = desc.cameraPosition + direction * CelestialBody::SKY_RENDER_DISTANCE;
            glow.halfSize = discRadius * glowRadius;
            glow.extent = glowRadius;
            glow.glowIntensity = glowIntensity;
            glow.glowRadius = glowRadius;
            drawFlareBillboard(u, glow);
        }

        // (2) 光条: 手前の物体に遮られる。深度テストを行い、深度は書き込まない。
        //     円盤自身に隠れないよう、ビルボードを円盤の最前面より手前へ寄せ、同じ比率で縮めて
        //     見かけの大きさを保つ（視線に垂直な平面の深度 = 距離 × facing で比べられるため）。
        if (spikeIntensity > 0.0f) {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            const float safeFacing = std::max(facing, 0.05f);
            const float distance = std::max(
                CelestialBody::SKY_RENDER_DISTANCE - SPIKE_DISC_CLEARANCE * discRadius / safeFacing,
                SPIKE_MIN_DISTANCE);
            const float scale = distance / CelestialBody::SKY_RENDER_DISTANCE;
            FlareBillboard spikes;
            spikes.center = desc.cameraPosition + direction * distance;
            spikes.halfSize = discRadius * spikeLength * scale;
            spikes.extent = spikeLength;
            spikes.spikeIntensity = spikeIntensity;
            spikes.spikeLength = spikeLength;
            spikes.spikeRotation = spikeRotation;
            spikes.spikeCount = spikeCount;
            drawFlareBillboard(u, spikes);
        }

        // (3) ベール: カメラが天体を向くほど画面全体が白く霞む（全画面なので遮蔽はしない）。
        //     天体から大きく外れた向きでは描かない。
        if (veilIntensity > 0.0f && facing > -0.2f) {
            glDisable(GL_DEPTH_TEST);
            glUniform1f(u.fullscreen, 1.0f);
            glUniform1f(u.veilIntensity, veilIntensity);
            glUniform1f(u.veilFalloff, veilFalloff);
            glUniform3f(u.bodyDirection, direction.x, direction.y, direction.z);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
    }

    // 呼び出し元（パーティクル等と同じ前提）の状態へ戻す。
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (!blendWasEnabled) glDisable(GL_BLEND);
    if (depthTestWasEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthFunc(static_cast<GLenum>(depthFuncWas));
    glDepthMask(depthMaskWas);
    glBindVertexArray(static_cast<GLuint>(previousVao));
    glUseProgram(static_cast<GLuint>(previousProgram));
}
