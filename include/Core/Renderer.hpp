#pragma once
#include <windows26.h>
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
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <memory>
#include <functional>

#include <include/imgui/imgui.h>
#include <include/imgui/imgui_impl_glfw.h>
#include <include/imgui/imgui_impl_opengl3.h>
#include <include/imgui/ImGuizmo.h>

// 前方宣言（循環インクルード回避）
class IEditorManager;
class GuiButton;
class SurfaceGui;

struct ViewportRenderDesc {
    GLuint fbo = 0;
    int width = 0;
    int height = 0;
    Vector3 cameraPosition;
    Vector3 cameraForward;
    Vector3 cameraUp;
    Workspace* workspace = nullptr;
    bool renderShadows = true;
    bool renderHighlights = false;
    bool renderConstraints = true;
    bool renderPostEffects = true;
    bool isFocused = false;
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

        unsigned int shadowFBO     = 0;
        unsigned int shadowMapTex  = 0;
        unsigned int depthShader   = 0;
        static const int SHADOW_MAP_SIZE = 2048;

        std::vector<unsigned int> indices = {};

        std::map<std::string, unsigned int> textureCache;

        unsigned int whiteTexture;
        void createWhiteTexture();

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
        void renderConstraints(Workspace& workspace, const Matrix4& view, const Matrix4& projection);

        // 地形ブラシのヒット位置ガイド（水平リング）。呼び出し側でFBOバインド・ビューポート設定済みであること。
        void renderBrushMarker(const Matrix4& view, const Matrix4& projection, const Vector3& center, float radius);

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

        // 雲（Weather）。カメラ追従の巨大水平クアッドに、起動時1回だけ焼いたグレースケール
        // ノイズテクスチャをWindDirectionでスクロールしながらサンプルする。ノイズ計算自体は
        // 焼き込み時にCPU側で完結しており、シェーダーはサンプル+しきい値のみの単純な構成。
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
        void renderLightning(Workspace& workspace, const Matrix4& view, const Matrix4& projection);

        // GUI 描画
        Matrix4  m_lastView;
        Matrix4  m_lastProj;
        std::function<void(GuiButton*)> m_onButtonActivated;

        void renderScreenGui(Workspace& ws, float vpX, float vpY, float vpW, float vpH);
        void renderWorldGui (Workspace& ws, User* user, float vpX, float vpY, float vpW, float vpH);
        void renderToolHotbar(User& user, float vpX, float vpY, float vpW, float vpH);
        void renderGameGui(Workspace& ws, User* user, float vpX, float vpY, float vpW, float vpH);
        void bakeSurfaceGui (SurfaceGui* sg);

        // カメラ回転ドラッグ中、非表示のOSカーソルの代わりにアンカー位置へ固定表示する擬似カーソル
        void drawCameraRotationCursor(User& user, GLFWwindow* window);

        // ポストエフェクト（PostEffect インスタンスの ZIndex 順チェーン適用）
        GLuint m_postVAO = 0, m_postVBO = 0;
        GLuint m_postShader = 0;
        GLuint m_postFboA = 0, m_postTexA = 0;
        GLuint m_postFboB = 0, m_postTexB = 0;
        int    m_postFboWidth = 0, m_postFboHeight = 0;

        void initPostEffectRenderer();
        void ensurePostEffectFBOs(int width, int height);
        void renderPostEffects(Workspace& workspace, GLuint targetFbo, int width, int height);

    private:
        void renderTerrain(const Matrix4& view, const Matrix4& projection, class Workspace* workspace);
};