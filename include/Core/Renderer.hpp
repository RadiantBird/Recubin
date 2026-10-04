#pragma once
#include <include/GL/glew.h>
#include <include/GLFW/glfw3.h>

#include <include/Math/Matrix4.hpp>
#include <include/Math/Vector2.hpp>
#include <include/Math/Quaternion.hpp>

#include <include/Core/User.hpp>
#include <include/Core/Terrain.hpp>
#include <include/Instances/Cube.hpp>
#include <include/Instances/Workspace.hpp>

#include <iostream>
#include <array>
#include <cstddef>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <memory>
#include <functional>
#include <filesystem>

#include <include/imgui/imgui.h>
#include <include/imgui/imgui_impl_glfw.h>
#include <include/imgui/imgui_impl_opengl3.h>
#include <include/imgui/ImGuizmo.h>
#include <Core/RuntimeChatOverlay.hpp>

// 前方宣言（循環インクルード回避）
class IEditorManager;
class ChatService;
class Decal;
class Skybox;
class Sun;
class GuiButton;
class SurfaceGui;
class ScreenGuiObject;

// 素のプリミティブ形状（デカール等なし・不透明）のインスタンス描画用データ
struct CubeInstanceData {
    float model[16]; // ワールド行列（Matrix4::m と同レイアウト）
    float color[4];  // RGBA
    float pbr[4];    // Material参照時: metallic, roughness, reflectance, 1。未参照は全て0（従来のLambert描画）
};

// Shadow depth pass用。深度シェーダーはdrawだけを使い、中心と半径は
// Cascadeごとのlight-space frustum cullingにだけ使用する。
struct ShadowInstanceData {
    CubeInstanceData draw;
    Vector3 center;
    float radius = 0.0f;
};

struct ViewportRenderDesc {
    GLuint fbo = 0;
    int width = 0;
    int height = 0;
    Vector3 cameraPosition;
    Vector3 cameraForward;
    Vector3 cameraUp;
    Workspace* workspace = nullptr;
    Instance* primarySelection = nullptr;                      // 非所有。Editor選択外枠用
    const std::vector<Instance*>* selectionTargets = nullptr; // 非所有。複数選択用
    bool renderShadows = true;
    bool renderHighlights = false;
    bool renderInstanceHighlights = true;  // Highlightインスタンス自体の描画。エディタ選択ハイライト(renderHighlights)とは独立。ランタイム単体でも常にtrue
    bool renderConstraints = true;
    bool renderPostEffects = true;
    bool renderPhysicsDebug = false; // 物理制約のデバッグビジュアライザー（エディターのViewメニューで切替）
    bool renderRenderingDebug = false; // SurfaceMark 等の描画デバッグビジュアライザー
    bool isFocused = false;
};

// ImGui上へゲームGUIを描画する際の投影情報。
// viewport* はImGuiの論理座標、projectionは3D描画と同じアスペクト比で構築する。
struct GameGuiRenderContext {
    float viewportX = 0.0f;
    float viewportY = 0.0f;
    float viewportWidth = 0.0f;
    float viewportHeight = 0.0f;
    float projectionAspect = 1.0f;

    Matrix4 view;
    Matrix4 projection;
    Vector3 cameraPosition;
    Vector3 cameraForward;
    Vector3 cameraRight;
    Vector3 cameraUp;

    // セカンダリViewportがUser.GetMouseRay用のプライマリ矩形を上書きしないための指定。
    bool recordUserViewport = true;
};

class Renderer {
    public:
        static Renderer* instance;
        unsigned int VBO;
        unsigned int VAO;
        unsigned int EBO;

        unsigned int shaderProgram;

        int          lightDirLoc    = -1;
        int          brightnessLoc  = -1;
        int          lightColorLoc  = -1;

        // メインシェーダー(shaderProgram)のuniform locationキャッシュ。
        // 毎フレーム/毎インスタンスのglGetUniformLocation(文字列検索)を避けるため、
        // init()でリンク直後に一度だけ取得する。
        int          viewLoc             = -1;
        int          projectionLoc       = -1;
        int          viewPosLoc          = -1;
        int          shadowDistanceLoc   = -1;
        int          shadowFadeDistanceLoc = -1;
        int          hasShadowsLoc       = -1;
        int          lightSpaceMatricesLoc = -1;
        int          shadowCascadeSplitsLoc = -1;
        int          shadowCascadeBlendLoc = -1;
        int          modelLoc            = -1;
        int          unlitLoc            = -1;
        int          triplanarLoc        = -1;
        int          texScaleLoc         = -1;
        int          uTimeLoc            = -1;
        int          uIsLiquidLoc        = -1;
        int          useVertexColorLoc   = -1;
        int          ourColorLoc         = -1;
        // SurfaceMark projection overlay uniforms (set only during the overlay pass).
        int          surfaceMarkPassLoc = -1;
        int          surfaceMarkWorldToLocalLoc = -1;
        int          surfaceMarkProjectionLoc = -1;
        int          surfaceMarkSizeLoc = -1;
        int          surfaceMarkTintLoc = -1;
        int          surfaceMarkDepthLoc = -1;
        int          surfaceMarkTextureLoc = -1;

        int          m_uPbrMaterialLoc   = -1;  // 個別描画用のPBR値(w=0でPBR無効)
        int          m_uEnvMaxLodLoc     = -1;

        unsigned int m_instanceVBO = 0;  // 毎フレーム上書きするインスタンスバッファ（全形状共有）

        // 素のプリミティブ形状のGPUインスタンシング用バッチ（Cube/Cylinder/Sphere/TriangularPrismの4種）
        static constexpr int INST_SHAPE_COUNT = 4;
        struct InstanceBatch {
            std::vector<CubeInstanceData> main;    // メインパス用（フラスタム内）
            std::vector<ShadowInstanceData> shadow; // シャドウパス用（CastShadow）
            bool attribsAttached = false;          // 形状のs_VAOへ属性5-9を付与済みか
        };
        InstanceBatch m_instBatches[INST_SHAPE_COUNT];
        int m_uInstancedLoc = -1;       // メインシェーダーの uInstanced
        int m_uInstancedDepthLoc = -1;  // depthシェーダーの uInstanced
        int m_uTimeDepthLoc = -1;
        int m_uIsLiquidDepthLoc = -1;

        struct LightUniformLocs {
            int type = -1, position = -1, direction = -1, color = -1;
            int brightness = -1, range = -1, cosCutoff = -1;
        };
        static const int MAX_LIGHTS = 8; // shaders/fragment.glslの#define MAX_LIGHTSと一致させること
        LightUniformLocs lightLocs[MAX_LIGHTS];
        int          uLightCountLoc = -1;

        unsigned int shadowFBO     = 0;
        unsigned int shadowMapTex  = 0;
        unsigned int depthShader   = 0;
        static constexpr int SHADOW_CASCADE_COUNT = 3;
        static const int SHADOW_MAP_SIZE = 2048;
        unsigned int surfaceMarkFBO = 0;
        unsigned int surfaceMarkDepthTex = 0;
        static const int SURFACE_MARK_MAP_SIZE = 1024;

        std::vector<unsigned int> indices = {};

        struct TextureCacheEntry {
            unsigned int textureID = 0;
            std::filesystem::file_time_type lastWriteTime{};
            uintmax_t fileSize = 0;
            bool hasFileMetadata = false;
        };
        std::map<std::string, TextureCacheEntry> textureCache;

        // ---- PBRの環境反射（Renderer_Environment.cpp） ----
        // Skyboxの6面から焼いた、mipがroughnessに対応するキューブマップ。Skyboxが無い/
        // テクスチャ未読込のときは手続き的な空のグラデーションを使う。メインシェーダーでは
        // テクスチャユニット ENV_SPECULAR_UNIT の samplerCube uEnvSpecular に束縛する。
        static constexpr int ENV_SPECULAR_UNIT = 10;
        unsigned int m_envSpecularTex = 0;
        float        m_envMaxLod = 0.0f;
        std::size_t  m_envSignature = 0;       // 焼いた時のSkybox入力の識別値。0は手続き的フォールバック
        bool         m_envBuilt = false;
        unsigned int m_envPrefilterShader = 0;
        unsigned int m_envFbo = 0;
        void initEnvironmentRenderer();
        void destroyEnvironmentRenderer();
        // Skyboxの入力が変わったときだけキューブマップを作り直す。呼び出し側のFBO・
        // viewport・各GL状態は保存して復元する。メインパスの直前に呼ぶこと。
        void updateEnvironmentMap(Skybox* skybox);
        // メインシェーダー(使用中)へ環境キューブマップとmax lodを設定する。
        void bindEnvironmentMap();

        // ---- 太陽・月のオーバーレイ: 逆光(グロー+ベール)・光条(スパイク)・月の満ち欠け
        //      （Renderer_Sky.cpp, sky_flare シェーダー） ----
        // 不透明物の描画後に重ねる。グロー・光条・月の円盤は、ビューポートの深度をコピーした
        // テクスチャ(m_skyDepthTex)で手前の物体に遮られ、グローだけは天体が見えている分だけ
        // 物体の上へもにじむ。ベールは天体が隠れているほど弱まる。
        unsigned int m_skyFlareShader = 0;
        struct SkyFlareUniforms {
            int fullscreen = -1, phaseDisc = -1, view = -1, projection = -1;
            int center = -1, right = -1, up = -1, halfSize = -1;
            int forward = -1, tanHalfFov = -1, aspect = -1;
            int color = -1, strength = -1, extent = -1;
            int glowIntensity = -1, glowRadius = -1;
            int spikeIntensity = -1, spikeLength = -1, spikeRotation = -1, spikeCount = -1;
            int veilIntensity = -1, veilFalloff = -1, bodyDirection = -1;
            int sceneDepth = -1, occlusionEnabled = -1, projA = -1, projB = -1;
            int occluderDepth = -1, discRadius = -1, viewportSize = -1, bleed = -1;
            int phase = -1, phaseRotation = -1, earthshine = -1;
        };
        SkyFlareUniforms m_skyFlareLoc;
        // 遮蔽判定用: ビューポートの深度のコピー。ビューポートの大きさが変わったら作り直す。
        unsigned int m_skyDepthTex = 0;
        unsigned int m_skyDepthFbo = 0;
        int m_skyDepthWidth = 0;
        int m_skyDepthHeight = 0;
        bool m_skyDepthBlitFailed = false;  // 深度のコピーが失敗した環境では遮蔽なしで描く
        void initSkyFlareRenderer();
        void destroySkyFlareRenderer();
        // sourceFbo の深度を m_skyDepthTex へコピーする。成功したら真。失敗したら以後は遮蔽なしにする。
        bool updateSkySceneDepth(unsigned int sourceFbo, int width, int height);

        unsigned int whiteTexture;
        void createWhiteTexture();
        unsigned int getMeshFallbackTexture() const { return m_meshFallbackTexture; }

        std::string loadShaderSource(const char* filePath);

        // Editor 管理
        std::unique_ptr<IEditorManager> editor;
        GLFWwindow* m_window = nullptr;

        void init(GLFWwindow* window);
        virtual ~Renderer();

        // 統合されたビューポート描画メソッド
        void renderViewport(const ViewportRenderDesc& desc);

        // メインループから呼ぶ統合描画
        void render(User &user, GLFWwindow* window, Workspace &workspace);

        void renderImGui(User &user, GLFWwindow* window, Workspace &workspace);

        unsigned int loadTexture(const char* path);
        unsigned int loadTextureFromMemory(const unsigned char* data, size_t size);

        // 制約ビジュアライザ（Rope/Rod）
        GLuint m_lineVAO    = 0;
        GLuint m_lineVBO    = 0;
        GLuint m_lineShader = 0;
        void initLineRenderer();
        void renderConstraints(Workspace& workspace, const Matrix4& view, const Matrix4& projection, const Vector3& cameraPosition);

        // 物理制約デバッグビジュアライザー（Weld/Motor/Attachment/Force。デフォルトOFF）
        void renderPhysicsDebug(Workspace& workspace, const Matrix4& view, const Matrix4& projection, const Vector3& cameraPosition);

        // 地形ブラシのヒット位置ガイド（ヒット面法線に直交するリング）。呼び出し側でFBOバインド・ビューポート設定済みであること。
        void renderBrushMarker(const Matrix4& view, const Matrix4& projection, const Vector3& center, float radius, const Vector3& cameraPosition, const Vector3& normal);

        // パーティクル（ParticleEmitter）。カメラ常時正面のビルボードをCPU側で頂点展開し、
        // テクスチャなし・単色頂点の専用シェーダーで描画する。シミュレーション自体はここでは
        // 行わない（ParticleEmitter::updateAllがメインループから毎フレーム1回呼ぶ。renderViewportは
        // ビューポートの数だけ複数回呼ばれるため、ここで状態を進めると多重更新になる）。
        GLuint m_particleVAO    = 0;
        GLuint m_particleVBO    = 0;
        GLuint m_particleShader = 0;
        void initParticleRenderer();
        void renderParticles(Workspace& workspace, const Matrix4& view, const Matrix4& projection,
                              const Vector3& cameraRight, const Vector3& cameraUp);

        // 雲（Weather）。CloudHeightに固定したワールドYの巨大水平クアッドに、起動時1回だけ
        // 焼いたグレースケールノイズテクスチャをWindDirectionでスクロールしながらサンプルする。
        // ノイズ計算自体は焼き込み時にCPU側で完結しており、シェーダーは色・サンプル・しきい値のみ。
        GLuint m_cloudVAO      = 0;
        GLuint m_cloudVBO      = 0;
        GLuint m_cloudShader   = 0;
        GLuint m_cloudNoiseTex = 0;
        void initCloudRenderer();
        void renderClouds(Workspace& workspace, const Matrix4& view, const Matrix4& projection,
                           const Vector3& cameraPosition);

        // 雷柱（Weatherが落雷時に中点変位法で生成したジグザグ頂点列を描画するだけ。
        // ジオメトリ生成自体はWeather::attemptStrike()側で行う）。既存のm_lineShaderを流用し
        // 新規GLリソースは追加しない。
        void renderLightning(Workspace& workspace, const Matrix4& view, const Matrix4& projection, const Vector3& cameraPosition);

        // GUI 描画
        std::function<void(GuiButton*)> m_onButtonActivated;
        std::weak_ptr<ChatService> m_chatService;
        RuntimeChatOverlay m_chatOverlay;

        void renderScreenGui(Workspace& ws, float vpX, float vpY, float vpW, float vpH);
        void renderWorldGui (Workspace& ws, User* user, const GameGuiRenderContext& context);
        void renderToolHotbar(User& user, float vpX, float vpY, float vpW, float vpH);
        static GameGuiRenderContext makeGameGuiRenderContext(
            float vpX, float vpY, float vpW, float vpH,
            const Vector3& cameraPosition,
            const Vector3& cameraForward,
            const Vector3& cameraRight,
            const Vector3& cameraUp,
            float projectionAspect,
            bool recordUserViewport = true);
        void renderGameGui(Workspace& ws, User* user, const GameGuiRenderContext& context);
        void renderRuntimeChat(float vpX, float vpY, float vpW, float vpH);
        // ImGui::NewFrame() より前に FontFile を atlas へ追加する。
        // 描画中の atlas 更新は避け、UTF-8 パスを持つ外部フォントもここで解決する。
        void prepareGuiFonts(Workspace& workspace);
        // Fixed-width font shared by the editor's auxiliary code/text panels.
        // It is loaded once during init() and remains separate from the
        // DotGothic16 UI default font.
        ImFont* codeEditorFont() const { return m_codeEditorFont; }
        void setChatService(const std::shared_ptr<ChatService>& service) { m_chatService = service; }
        bool isChatCapturingKeyboard() const { return m_chatOverlay.isCapturingKeyboard(); }
        void bakeSurfaceGui (SurfaceGui* sg);
        ImFont* resolveGuiFont(ScreenGuiObject* sgo);
        ImFont* loadGuiFont(ScreenGuiObject* sgo);
        std::map<std::wstring, ImFont*> m_guiFontCache;
        ImFont* m_systemDefaultGuiFont = nullptr;
        ImFont* m_dotGothicGuiFont = nullptr;
        ImFont* m_mplusGuiFont = nullptr;  // UI / SystemFont::Default の既定フォント
        ImFont* m_codeEditorFont = nullptr;

        // カメラ回転ドラッグ中、非表示のOSカーソルの代わりにアンカー位置へ固定表示する擬似カーソル
        void drawCameraRotationCursor(User& user, GLFWwindow* window);

        // ポストエフェクト（PostEffect インスタンスの ZIndex 順チェーン適用）
        GLuint m_postVAO = 0, m_postVBO = 0;
        GLuint m_postShader = 0;
        GLuint m_postFboA = 0, m_postTexA = 0;
        GLuint m_postFboB = 0, m_postTexB = 0;
        int    m_postFboWidth = 0, m_postFboHeight = 0;

        // Screen-space editor selection mask and outline resources.
        GLuint m_selectionMaskFBO = 0;
        GLuint m_selectionMaskTex = 0;
        GLuint m_selectionMaskDepth = 0;
        GLuint m_selectionMaskShader = 0;
        GLuint m_selectionOutlineShader = 0;
        int m_selectionMaskWidth = 0;
        int m_selectionMaskHeight = 0;

        struct CustomPostEffectProgram {
            GLuint program = 0;
            std::filesystem::file_time_type lastWriteTime{};
            bool hasMetadata = false;
            bool attempted = false;
        };
        std::map<std::string, CustomPostEffectProgram> m_customPostEffectPrograms;

        void initPostEffectRenderer();
        void ensurePostEffectFBOs(int width, int height);
        void renderPostEffects(Workspace& workspace, GLuint targetFbo, int width, int height);
        void initSelectionRenderer();
        void ensureSelectionMaskFBO(int width, int height);
        void renderEditorSelectionOutline(const ViewportRenderDesc& desc,
                                          const Matrix4& view, const Matrix4& projection);

    private:
        unsigned int m_meshFallbackTexture = 0;
        void createMeshFallbackTexture();

        static constexpr std::size_t GPU_QUERY_FRAME_COUNT = 4;
        enum class GpuTimestamp : std::size_t {
            TotalBegin,
            ShadowBegin,
            ShadowEnd,
            MainBegin,
            MainEnd,
            SurfaceMarksBegin,
            SurfaceMarksEnd,
            ExtrasBegin,
            ExtrasEnd,
            TotalEnd,
            Count
        };
        static constexpr std::size_t GPU_TIMESTAMP_COUNT =
            static_cast<std::size_t>(GpuTimestamp::Count);
        struct GpuQueryFrame {
            std::array<GLuint, GPU_TIMESTAMP_COUNT> queries{};
            bool pending = false;
            bool hasViewportSample = false;
        };
        std::array<GpuQueryFrame, GPU_QUERY_FRAME_COUNT> m_gpuQueryFrames{};
        bool m_gpuTimingSupported = false;
        bool m_gpuViewportSampled = false;
        int m_gpuActiveQueryFrame = -1;

        void initGpuProfiler();
        void destroyGpuProfiler();
        void pollGpuProfiler();
        void beginGpuFrame();
        void endGpuFrame();
        bool beginGpuViewportSample();
        void writeGpuTimestamp(GpuTimestamp timestamp);

        void renderTerrain(const Matrix4& view, const Matrix4& projection, class Workspace* workspace);

        // 形状の共有VAOにインスタンス属性(5-9, divisor=1)を後付けする
        void attachInstanceAttribs(unsigned int vao);

    public:
        // Render a screen-space outline for visible geometry into an existing viewport FBO.
        // The destination depth buffer is copied into the renderer-owned mask depth buffer,
        // so foreground geometry continues to occlude the transient outline.
        void renderSelectionOutline(const std::vector<BaseCube*>& targets,
                                     GLuint destinationFbo, int width, int height,
                                     const Matrix4& view, const Matrix4& projection,
                                     const Color4& outlineColor, float outlineWidth);

    private:

        // 太陽と月（CelestialBody）をカメラ基準の空の円盤として描く（Renderer_Sky.cpp）。メインパス内で、
        // メインシェーダー使用中に呼ぶ。位置は常にカメラから一定距離、見かけの大きさは Distance で決まる。
        // primarySunが無いとき、Moonは既定のSun角の反対側に描く。
        void renderCelestialBodies(Workspace& workspace, const Vector3& cameraPosition,
                                   const Sun* primarySun);

        // 太陽・月の逆光・光条・月の満ち欠けを、シーンの上に重ねる。ポストエフェクトの前、シーン描画の最後に呼ぶ。
        void renderSkyFlares(Workspace& workspace, const ViewportRenderDesc& desc,
                             const Matrix4& view, const Matrix4& projection,
                             float fovYDegrees, const Sun* primarySun);

        // Decal が直接貼り付く Cube Face の4辺だけを、エディタ選択表示として描画する。
        void drawDecalFaceHighlight(Decal* decal, const Color4& outlineColor,
                                    float outlineThickness, const Matrix4& view,
                                    const Matrix4& projection, const Vector3& cameraPosition,
                                    float fovYDegrees, int viewportHeightPx);

        // 塗り+輪郭のハイライト描画（深度テスト無効）。1つのBaseCubeターゲットに対して呼ぶ。
        // Highlightインスタンス描画・エディタ選択ハイライトの両方から共有される
        void drawBaseCubeHighlight(BaseCube* target, const Color4& fillColor,
                                    const Color4& outlineColor, float outlineThickness,
                                    const Matrix4& view, const Matrix4& projection,
                                    const Vector3& cameraPosition, float fovYDegrees,
                                    int viewportHeightPx);

        // ワークスペース全体からHighlightインスタンスを収集し、それぞれ有効なら描画する
        void renderInstanceHighlights(Workspace& workspace, const Matrix4& view, const Matrix4& projection,
                                       const Vector3& cameraPosition, float fovYDegrees, int viewportHeightPx);
};
