#include <Core/MainShaderState.hpp>
#include <Util/Logger.hpp>
#include <GL/glew.h>
#include <sokol/sokol_gfx.h>
#include <shaders/main.h>

#include <algorithm>
#include <cstring>

struct MainShaderState::Blocks {
    main_vs_frame_t vsFrame = {};
    main_vs_object_t vsObject = {};
    main_fs_frame_t fsFrame = {};
    main_fs_object_t fsObject = {};
    main_fs_pass_t fsPass = {};
    main_fs_decals_t fsDecals = {};
};

static_assert(sizeof(main_fs_frame_t::uLightPosRange) / sizeof(main_fs_frame_t::uLightPosRange[0]) ==
              MainShaderState::MAX_LIGHTS);
static_assert(sizeof(main_fs_decals_t::uDecalColor) / sizeof(main_fs_decals_t::uDecalColor[0]) ==
              MainShaderState::MAX_DECALS);
static_assert(VIEW_main_ourTexture == MainShaderState::BASE_TEXTURE_UNIT);
static_assert(VIEW_main_shadowMap == MainShaderState::SHADOW_MAP_UNIT);
static_assert(VIEW_main_uSurfaceMarkTexture == MainShaderState::SURFACE_MARK_TEXTURE_UNIT);
static_assert(VIEW_main_uSurfaceMarkDepth == MainShaderState::SURFACE_MARK_DEPTH_UNIT);
static_assert(VIEW_main_uDecalTexArray == MainShaderState::DECAL_ARRAY_UNIT);
static_assert(VIEW_main_uEnvSpecular == MainShaderState::ENV_SPECULAR_UNIT);
static_assert(VIEW_main_uMatBaseColorMap == MainShaderState::MATERIAL_MAP_UNIT_BASE + 0);
static_assert(VIEW_main_uMatRoughnessMap == MainShaderState::MATERIAL_MAP_UNIT_BASE + 1);
static_assert(VIEW_main_uMatMetallicMap == MainShaderState::MATERIAL_MAP_UNIT_BASE + 2);
static_assert(VIEW_main_uMatNormalMap == MainShaderState::MATERIAL_MAP_UNIT_BASE + 3);

namespace {

void copyMatrix(float (&dst)[16], const Matrix4& src) {
    std::memcpy(dst, src.m, sizeof(dst));
}

void copyVec3(float (&dst)[3], const Vector3& v) {
    dst[0] = v.x;
    dst[1] = v.y;
    dst[2] = v.z;
}

void copyColor(float (&dst)[4], const Color4& c) {
    dst[0] = c.r;
    dst[1] = c.g;
    dst[2] = c.b;
    dst[3] = c.a;
}

bool validIndex(int index, int count, const char* what) {
    if (index >= 0 && index < count) return true;
    RCBN_ERROR("MainShaderState: " << what << " index " << index << " is out of range (0.." << count - 1 << ")");
    return false;
}

} // namespace

MainShaderState::MainShaderState() : m_blocks(std::make_unique<Blocks>()) {}

MainShaderState::~MainShaderState() = default;

bool MainShaderState::create() {
    *m_blocks = Blocks{};
    m_dirty = DIRTY_ALL;
    return m_program.create(*main_draw_shader_desc(SG_BACKEND_GLCORE), "main",
                            ShdcProgram::TextureUnits::ViewSlot);
}

void MainShaderState::destroy() {
    m_program.destroy();
}

bool MainShaderState::isValid() const {
    return m_program.isValid();
}

unsigned int MainShaderState::program() const {
    return m_program.program();
}

void MainShaderState::use() const {
    m_program.use();
}

void MainShaderState::flush() {
    if (!m_dirty || !m_program.isValid()) return;
    const Blocks& b = *m_blocks;
    if (m_dirty & DIRTY_VS_FRAME) m_program.applyUniforms(UB_main_vs_frame, b.vsFrame);
    if (m_dirty & DIRTY_VS_OBJECT) m_program.applyUniforms(UB_main_vs_object, b.vsObject);
    if (m_dirty & DIRTY_FS_FRAME) m_program.applyUniforms(UB_main_fs_frame, b.fsFrame);
    if (m_dirty & DIRTY_FS_OBJECT) m_program.applyUniforms(UB_main_fs_object, b.fsObject);
    if (m_dirty & DIRTY_FS_PASS) m_program.applyUniforms(UB_main_fs_pass, b.fsPass);
    if (m_dirty & DIRTY_FS_DECALS) m_program.applyUniforms(UB_main_fs_decals, b.fsDecals);
    m_dirty = 0;
}

// ---- frame ----
void MainShaderState::setView(const Matrix4& view) {
    copyMatrix(m_blocks->vsFrame.view, view);
    m_dirty |= DIRTY_VS_FRAME;
}

void MainShaderState::setProjection(const Matrix4& projection) {
    copyMatrix(m_blocks->vsFrame.projection, projection);
    m_dirty |= DIRTY_VS_FRAME;
}

void MainShaderState::setTime(float seconds) {
    m_blocks->vsFrame.uTime = seconds;
    m_dirty |= DIRTY_VS_FRAME;
}

void MainShaderState::setViewPos(const Vector3& position) {
    copyVec3(m_blocks->fsFrame.viewPos, position);
    m_dirty |= DIRTY_FS_FRAME;
}

void MainShaderState::setLightDir(const Vector3& direction) {
    copyVec3(m_blocks->fsFrame.lightDir, direction);
    m_dirty |= DIRTY_FS_FRAME;
}

void MainShaderState::setBrightness(float brightness) {
    m_blocks->fsFrame.brightness = brightness;
    m_dirty |= DIRTY_FS_FRAME;
}

void MainShaderState::setLightColor(const Color4& color) {
    m_blocks->fsFrame.lightColor[0] = color.r;
    m_blocks->fsFrame.lightColor[1] = color.g;
    m_blocks->fsFrame.lightColor[2] = color.b;
    m_dirty |= DIRTY_FS_FRAME;
}

void MainShaderState::setLightSpaceMatrices(const Matrix4* matrices, int count) {
    const int maxCount = static_cast<int>(sizeof(main_fs_frame_t::lightSpaceMatrices) /
                                          sizeof(main_fs_frame_t::lightSpaceMatrices[0]));
    if (!matrices || count < 0 || count > maxCount) {
        RCBN_ERROR("MainShaderState: invalid light-space matrix count " << count << " (max " << maxCount << ")");
        return;
    }
    for (int i = 0; i < count; ++i) copyMatrix(m_blocks->fsFrame.lightSpaceMatrices[i], matrices[i]);
    m_dirty |= DIRTY_FS_FRAME;
}

void MainShaderState::setShadowCascadeSplits(float split0, float split1, float split2) {
    m_blocks->fsFrame.uShadowCascadeSplits[0] = split0;
    m_blocks->fsFrame.uShadowCascadeSplits[1] = split1;
    m_blocks->fsFrame.uShadowCascadeSplits[2] = split2;
    m_dirty |= DIRTY_FS_FRAME;
}

void MainShaderState::setShadowCascadeBlend(float blend0, float blend1) {
    m_blocks->fsFrame.uShadowCascadeBlend[0] = blend0;
    m_blocks->fsFrame.uShadowCascadeBlend[1] = blend1;
    m_dirty |= DIRTY_FS_FRAME;
}

void MainShaderState::setShadowDistance(float distance) {
    m_blocks->fsFrame.uShadowDistance = distance;
    m_dirty |= DIRTY_FS_FRAME;
}

void MainShaderState::setShadowFadeDistance(float distance) {
    m_blocks->fsFrame.uShadowFadeDistance = distance;
    m_dirty |= DIRTY_FS_FRAME;
}

void MainShaderState::setHasShadows(float hasShadows) {
    m_blocks->fsFrame.hasShadows = hasShadows;
    m_dirty |= DIRTY_FS_FRAME;
}

void MainShaderState::setEnvMaxLod(float maxLod) {
    m_blocks->fsFrame.uEnvMaxLod = maxLod;
    m_dirty |= DIRTY_FS_FRAME;
}

void MainShaderState::setLight(int index, bool spot, const Vector3& position, const Vector3& direction,
                               const Color4& color, float brightness, float range, float cosCutoff) {
    if (!validIndex(index, MAX_LIGHTS, "light")) return;
    main_fs_frame_t& f = m_blocks->fsFrame;
    f.uLightPosRange[index][0] = position.x;
    f.uLightPosRange[index][1] = position.y;
    f.uLightPosRange[index][2] = position.z;
    f.uLightPosRange[index][3] = range;
    f.uLightDirCutoff[index][0] = direction.x;
    f.uLightDirCutoff[index][1] = direction.y;
    f.uLightDirCutoff[index][2] = direction.z;
    f.uLightDirCutoff[index][3] = cosCutoff;
    f.uLightColorBright[index][0] = color.r;
    f.uLightColorBright[index][1] = color.g;
    f.uLightColorBright[index][2] = color.b;
    f.uLightColorBright[index][3] = brightness;
    f.uLightType[index][0] = spot ? 1.0f : 0.0f;
    m_dirty |= DIRTY_FS_FRAME;
}

void MainShaderState::setLightCount(int count) {
    m_blocks->fsFrame.uLightCount = static_cast<float>(std::clamp(count, 0, MAX_LIGHTS));
    m_dirty |= DIRTY_FS_FRAME;
}

// ---- object ----
void MainShaderState::setModel(const Matrix4& model) {
    copyMatrix(m_blocks->vsObject.model, model);
    m_dirty |= DIRTY_VS_OBJECT;
}

void MainShaderState::setPbrMaterial(float metallic, float roughness, float reflectance, float enabled) {
    m_blocks->vsObject.uPbrMaterial[0] = metallic;
    m_blocks->vsObject.uPbrMaterial[1] = roughness;
    m_blocks->vsObject.uPbrMaterial[2] = reflectance;
    m_blocks->vsObject.uPbrMaterial[3] = enabled;
    m_dirty |= DIRTY_VS_OBJECT;
}

void MainShaderState::setIsLiquid(float isLiquid) {
    m_blocks->vsObject.uIsLiquid = isLiquid;
    m_dirty |= DIRTY_VS_OBJECT;
}

void MainShaderState::setInstanced(float instanced) {
    m_blocks->vsObject.uInstanced = instanced;
    m_blocks->fsObject.uInstanced = instanced;
    m_dirty |= DIRTY_VS_OBJECT | DIRTY_FS_OBJECT;
}

void MainShaderState::setColor(const Color4& color) {
    copyColor(m_blocks->fsObject.ourColor, color);
    m_dirty |= DIRTY_FS_OBJECT;
}

void MainShaderState::setTextureTintColor(const Color4& color) {
    copyColor(m_blocks->fsObject.uTextureTintColor, color);
    m_dirty |= DIRTY_FS_OBJECT;
}

void MainShaderState::setUseTextureTint(float useTint) {
    m_blocks->fsObject.uUseTextureTint = useTint;
    m_dirty |= DIRTY_FS_OBJECT;
}

void MainShaderState::setMatMapFlags(float baseColor, float roughness, float metallic, float normal) {
    m_blocks->fsObject.uMatMapFlags[0] = baseColor;
    m_blocks->fsObject.uMatMapFlags[1] = roughness;
    m_blocks->fsObject.uMatMapFlags[2] = metallic;
    m_blocks->fsObject.uMatMapFlags[3] = normal;
    m_dirty |= DIRTY_FS_OBJECT;
}

void MainShaderState::setMatMapParams(float studsPerTile, float normalStrength) {
    m_blocks->fsObject.uMatMapParams[0] = studsPerTile;
    m_blocks->fsObject.uMatMapParams[1] = normalStrength;
    m_dirty |= DIRTY_FS_OBJECT;
}

void MainShaderState::setLocalBounds(const Vector3& boundsMin, const Vector3& boundsMax) {
    copyVec3(m_blocks->fsObject.uLocalBoundsMin, boundsMin);
    copyVec3(m_blocks->fsObject.uLocalBoundsMax, boundsMax);
    m_dirty |= DIRTY_FS_OBJECT;
}

void MainShaderState::setUnlit(float unlit) {
    m_blocks->fsObject.unlit = unlit;
    m_dirty |= DIRTY_FS_OBJECT;
}

void MainShaderState::setTextureScale(float scale) {
    m_blocks->fsObject.u_textureScale = scale;
    m_dirty |= DIRTY_FS_OBJECT;
}

void MainShaderState::setUvScale(float u, float v) {
    m_blocks->fsObject.uvScale[0] = u;
    m_blocks->fsObject.uvScale[1] = v;
    m_dirty |= DIRTY_FS_OBJECT;
}

void MainShaderState::setTriplanar(float useTriplanar) {
    m_blocks->fsObject.useTriplanar = useTriplanar;
    m_dirty |= DIRTY_FS_OBJECT;
}

void MainShaderState::setSurfaceGui(float isSurfaceGui) {
    m_blocks->fsObject.isSurfaceGui = isSurfaceGui;
    m_dirty |= DIRTY_FS_OBJECT;
}

void MainShaderState::setUseVertexColor(float useVertexColor) {
    m_blocks->fsObject.useVertexColor = useVertexColor;
    m_dirty |= DIRTY_FS_OBJECT;
}

// ---- pass ----
void MainShaderState::setSurfaceMarkPass(float pass) {
    m_blocks->fsPass.uSurfaceMarkPass = pass;
    m_dirty |= DIRTY_FS_PASS;
}

void MainShaderState::setSurfaceMarkWorldToLocal(const Matrix4& worldToLocal) {
    copyMatrix(m_blocks->fsPass.uSurfaceMarkWorldToLocal, worldToLocal);
    m_dirty |= DIRTY_FS_PASS;
}

void MainShaderState::setSurfaceMarkProjection(const Matrix4& projection) {
    copyMatrix(m_blocks->fsPass.uSurfaceMarkProjection, projection);
    m_dirty |= DIRTY_FS_PASS;
}

void MainShaderState::setSurfaceMarkSize(const Vector3& size) {
    copyVec3(m_blocks->fsPass.uSurfaceMarkSize, size);
    m_dirty |= DIRTY_FS_PASS;
}

void MainShaderState::setSurfaceMarkTint(const Color4& tint) {
    copyColor(m_blocks->fsPass.uSurfaceMarkTint, tint);
    m_dirty |= DIRTY_FS_PASS;
}

// ---- decals ----
void MainShaderState::setDecal(int index, int layer, float centerU, float centerV, float radius,
                               const Color4& color, int mode, int face) {
    if (!validIndex(index, MAX_DECALS, "decal")) return;
    main_fs_decals_t& d = m_blocks->fsDecals;
    d.uDecalCenterRadius[index][0] = centerU;
    d.uDecalCenterRadius[index][1] = centerV;
    d.uDecalCenterRadius[index][2] = radius;
    d.uDecalCenterRadius[index][3] = static_cast<float>(layer);
    copyColor(d.uDecalColor[index], color);
    d.uDecalModeFace[index][0] = static_cast<float>(mode);
    d.uDecalModeFace[index][1] = static_cast<float>(face);
    m_dirty |= DIRTY_FS_DECALS;
}

void MainShaderState::setDecalCount(int count) {
    m_blocks->fsDecals.uDecalCount = static_cast<float>(std::clamp(count, 0, MAX_DECALS));
    m_dirty |= DIRTY_FS_DECALS;
}
