#pragma once

#include <Core/ShdcProgram.hpp>
#include <Math/Matrix4.hpp>
#include <Math/Vector3.hpp>
#include <Util/Color4.hpp>

#include <memory>

// メインシェーダー（shaders/sokol/main.glsl）のユニフォームを CPU 側で保持し、描画の直前に送る。
//
// - 値は更新頻度ごとのブロック（frame / object / pass / decals）に分かれている。set* は値を書いて
//   そのブロックに印を付けるだけで、GL へは送らない。
// - メインシェーダーで描く glDraw* の直前に必ず flush() を呼ぶこと。印の付いたブロックだけを送る。
//   送らなかった値は前回 flush した値のまま残る（従来の glUniform と同じく、設定した値は持続する）。
// - flush() は glProgramUniform で送るので、どのプログラムが束縛中でもよい。
// - テクスチャはユニット番号（= main.glsl の binding）へ生 GL で束縛する。番号は *_UNIT 定数を使う。
class MainShaderState {
public:
    static constexpr int MAX_LIGHTS = 8;  // main.glsl の MAX_LIGHTS と一致させること
    static constexpr int MAX_DECALS = 8;  // main.glsl の MAX_DECALS と一致させること

    // テクスチャユニット（main.glsl のテクスチャの binding と一致）
    static constexpr int BASE_TEXTURE_UNIT = 0;
    static constexpr int SHADOW_MAP_UNIT = 1;
    static constexpr int SURFACE_MARK_TEXTURE_UNIT = 2;
    static constexpr int SURFACE_MARK_DEPTH_UNIT = 3;
    static constexpr int DECAL_ARRAY_UNIT = 4;
    static constexpr int ENV_SPECULAR_UNIT = 5;
    static constexpr int MATERIAL_MAP_UNIT_BASE = 6;  // BaseColor / Roughness / Metallic / Normal の 4 つ

    MainShaderState();
    ~MainShaderState();
    MainShaderState(const MainShaderState&) = delete;
    MainShaderState& operator=(const MainShaderState&) = delete;

    bool create();
    void destroy();
    bool isValid() const;
    unsigned int program() const;
    void use() const;
    // 印の付いたブロックを送る。メインシェーダーで描く直前に呼ぶ。
    void flush();

    // ---- frame（カメラ・光源・影・環境） ----
    void setView(const Matrix4& view);
    void setProjection(const Matrix4& projection);
    void setTime(float seconds);
    void setViewPos(const Vector3& position);
    void setLightDir(const Vector3& direction);
    void setBrightness(float brightness);
    void setLightColor(const Color4& color);  // rgb のみ使う
    void setLightSpaceMatrices(const Matrix4* matrices, int count);
    void setShadowCascadeSplits(float split0, float split1, float split2);
    void setShadowCascadeBlend(float blend0, float blend1);
    void setShadowDistance(float distance);
    void setShadowFadeDistance(float distance);
    void setHasShadows(float hasShadows);
    void setEnvMaxLod(float maxLod);
    // 追加光源（Point/Spot）。index は 0..MAX_LIGHTS-1
    void setLight(int index, bool spot, const Vector3& position, const Vector3& direction,
                  const Color4& color, float brightness, float range, float cosCutoff);
    void setLightCount(int count);

    // ---- object（描画ごと） ----
    void setModel(const Matrix4& model);
    void setPbrMaterial(float metallic, float roughness, float reflectance, float enabled);
    void setIsLiquid(float isLiquid);
    void setInstanced(float instanced);  // 頂点・フラグメントの両方に入れる
    void setColor(const Color4& color);
    void setTextureTintColor(const Color4& color);
    void setUseTextureTint(float useTint);
    void setMatMapFlags(float baseColor, float roughness, float metallic, float normal);
    void setMatMapParams(float studsPerTile, float normalStrength);
    void setLocalBounds(const Vector3& boundsMin, const Vector3& boundsMax);
    void setUnlit(float unlit);
    void setTextureScale(float scale);
    void setUvScale(float u, float v);
    void setTriplanar(float useTriplanar);
    void setSurfaceGui(float isSurfaceGui);
    void setUseVertexColor(float useVertexColor);

    // ---- pass（SurfaceMark の重ね描画） ----
    void setSurfaceMarkPass(float pass);
    void setSurfaceMarkWorldToLocal(const Matrix4& worldToLocal);
    void setSurfaceMarkProjection(const Matrix4& projection);
    void setSurfaceMarkSize(const Vector3& size);
    void setSurfaceMarkTint(const Color4& tint);

    // ---- decals（MeshCube の UV 空間 Decal）。index は 0..MAX_DECALS-1 ----
    void setDecal(int index, int layer, float centerU, float centerV, float radius, const Color4& color,
                  int mode, int face);
    void setDecalCount(int count);

private:
    struct Blocks;
    enum DirtyBit : unsigned {
        DIRTY_VS_FRAME = 1u << 0,
        DIRTY_VS_OBJECT = 1u << 1,
        DIRTY_FS_FRAME = 1u << 2,
        DIRTY_FS_OBJECT = 1u << 3,
        DIRTY_FS_PASS = 1u << 4,
        DIRTY_FS_DECALS = 1u << 5,
        DIRTY_ALL = (1u << 6) - 1u,
    };

    ShdcProgram m_program;
    std::unique_ptr<Blocks> m_blocks;
    unsigned m_dirty = DIRTY_ALL;
};
