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

void drawFlareBillboard(const Renderer::SkyFlareUniforms& u, const FlareBillboard& b) {
    glUniform1f(u.fullscreen, 0.0f);
    glUniform1f(u.phaseDisc, 0.0f);
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

        // 満ち欠けのある月は sky_flare の円盤で描く。シェーダーが無い環境では球で代用し、月を消さない。
        if (m_skyFlareShader && isPhasedMoon(*body)) continue;

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
    const std::string vertexSource = FileLoader::readText("shaders/sky_flare_vertex.glsl");
    const std::string fragmentSource = FileLoader::readText("shaders/sky_flare_fragment.glsl");
    if (vertexSource.empty() || fragmentSource.empty()) {
        RCBN_ERROR("Renderer: sky_flare shader sources are missing; sun/moon glow, veil, spikes and moon phases are disabled");
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
    m_skyFlareLoc.phaseDisc = loc("uPhaseDisc");
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
    m_skyFlareLoc.sceneDepth = loc("uSceneDepth");
    m_skyFlareLoc.occlusionEnabled = loc("uOcclusionEnabled");
    m_skyFlareLoc.projA = loc("uProjA");
    m_skyFlareLoc.projB = loc("uProjB");
    m_skyFlareLoc.occluderDepth = loc("uOccluderDepth");
    m_skyFlareLoc.discRadius = loc("uDiscRadius");
    m_skyFlareLoc.viewportSize = loc("uViewportSize");
    m_skyFlareLoc.bleed = loc("uBleed");
    m_skyFlareLoc.phase = loc("uPhase");
    m_skyFlareLoc.phaseRotation = loc("uPhaseRotation");
    m_skyFlareLoc.earthshine = loc("uEarthshine");
}

void Renderer::destroySkyFlareRenderer() {
    if (m_skyFlareShader) glDeleteProgram(m_skyFlareShader);
    if (m_skyDepthFbo) glDeleteFramebuffers(1, &m_skyDepthFbo);
    if (m_skyDepthTex) glDeleteTextures(1, &m_skyDepthTex);
    m_skyFlareShader = 0;
    m_skyDepthFbo = 0;
    m_skyDepthTex = 0;
}

bool Renderer::updateSkySceneDepth(unsigned int sourceFbo, int width, int height) {
    if (m_skyDepthBlitFailed || width <= 0 || height <= 0) return false;

    if (!m_skyDepthTex || width != m_skyDepthWidth || height != m_skyDepthHeight) {
        if (!m_skyDepthTex) glGenTextures(1, &m_skyDepthTex);
        glBindTexture(GL_TEXTURE_2D, m_skyDepthTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);  // 深度の値そのものを読む

        if (!m_skyDepthFbo) glGenFramebuffers(1, &m_skyDepthFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_skyDepthFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_skyDepthTex, 0);
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
    if (!m_skyFlareShader || !m_postVAO || desc.width <= 0 || desc.height <= 0) return;
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
    GLint previousProgram = 0, previousVao = 0, previousActiveTexture = GL_TEXTURE0, previousTexture = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    const GLboolean depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    GLboolean depthMaskWas = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMaskWas);

    glUseProgram(m_skyFlareShader);
    glBindVertexArray(m_postVAO);
    // 遮蔽はシェーダーが深度テクスチャで判定するので、ハードウェアの深度テストは使わない。
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);

    const SkyFlareUniforms& u = m_skyFlareLoc;
    const float aspect = static_cast<float>(desc.width) / static_cast<float>(desc.height);
    glUniformMatrix4fv(u.view, 1, GL_FALSE, view.m);
    glUniformMatrix4fv(u.projection, 1, GL_FALSE, projection.m);
    glUniform3f(u.right, right.x, right.y, right.z);
    glUniform3f(u.up, up.x, up.y, up.z);
    glUniform3f(u.forward, forward.x, forward.y, forward.z);
    glUniform1f(u.tanHalfFov, std::tan(fovYDegrees * 0.5f * DEGREES_TO_RADIANS));
    glUniform1f(u.aspect, aspect);

    // 遮蔽判定の共通設定。投影行列の m[10]/m[14] で、深度バッファの値を視線方向の距離へ戻す。
    glBindTexture(GL_TEXTURE_2D, occlusionAvailable ? m_skyDepthTex : 0);
    glUniform1i(u.sceneDepth, 0);
    glUniform1f(u.occlusionEnabled, occlusionAvailable ? 1.0f : 0.0f);
    glUniform1f(u.projA, projection.m[10]);
    glUniform1f(u.projB, projection.m[14]);
    glUniform2f(u.viewportSize, static_cast<float>(desc.width), static_cast<float>(desc.height));
    glUniform1f(u.bleed, GLOW_BLEED);

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
        glUniform3f(u.color, body->Color.r, body->Color.g, body->Color.b);
        glUniform3f(u.center, center.x, center.y, center.z);
        glUniform1f(u.discRadius, discRadius);
        glUniform1f(u.occluderDepth,
                    facing * CelestialBody::SKY_RENDER_DISTANCE - DISC_CLEARANCE * discRadius);

        // (1) 満ち欠けのある月の円盤（アルファブレンド）。影の部分は Earthshine だけ透けて空が見える。
        if (phased) {
            const Moon& moon = static_cast<const Moon&>(*body);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glUniform1f(u.fullscreen, 0.0f);
            glUniform1f(u.phaseDisc, 1.0f);
            glUniform1f(u.strength, body->Color.a);
            glUniform1f(u.halfSize, discRadius);
            glUniform1f(u.extent, 1.0f);
            glUniform1f(u.phase, moon.wrappedPhase());
            glUniform1f(u.phaseRotation, finiteOr(moon.PhaseRotation, 0.0f) * DEGREES_TO_RADIANS);
            glUniform1f(u.earthshine, std::clamp(finiteOr(moon.Earthshine, 0.0f), 0.0f, 1.0f));
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glUniform1f(u.phaseDisc, 0.0f);
        }

        if (!body->hasFlareEffect()) continue;
        const float strength = body->Color.a * horizonFadeFactor(*body, direction);
        if (strength <= 0.001f) continue;
        glUniform1f(u.strength, strength);
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
            drawFlareBillboard(u, billboard);
        }

        // (3) ベール: カメラが天体を向くほど画面全体が白く霞む。天体が隠れているほど弱い。
        //     天体から大きく外れた向きでは描かない。
        if (veilIntensity > 0.0f && facing > -0.2f) {
            glUniform1f(u.fullscreen, 1.0f);
            glUniform1f(u.veilIntensity, veilIntensity);
            glUniform1f(u.veilFalloff, veilFalloff);
            glUniform3f(u.bodyDirection, direction.x, direction.y, direction.z);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glUniform1f(u.fullscreen, 0.0f);
        }
    }

    // 呼び出し元（パーティクル等と同じ前提）の状態へ戻す。
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (!blendWasEnabled) glDisable(GL_BLEND);
    if (depthTestWasEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthMask(depthMaskWas);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    glBindVertexArray(static_cast<GLuint>(previousVao));
    glUseProgram(static_cast<GLuint>(previousProgram));
}
