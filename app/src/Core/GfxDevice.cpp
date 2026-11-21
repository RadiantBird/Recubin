#include <Core/GfxDevice.hpp>
#include <Util/Logger.hpp>
#include <GL/glew.h>
#include <sokol/sokol_gfx.h>

#include <algorithm>
#include <cstdint>
#include <vector>

// sokol のリセットが書き換える GL 状態（_sg_gl_reset_state_cache / _sg_gl_setup_backend）を
// すべて保存・復元する。項目を増減するときは sokol_gfx.h の該当関数と突き合わせること。
struct GlStateSnapshot {
    struct TextureUnit {
        GLint tex2D = 0;
        GLint texCube = 0;
        GLint tex3D = 0;
        GLint tex2DArray = 0;
        GLint sampler = 0;
    };
    struct StencilFace {
        GLint func = GL_ALWAYS;
        GLint ref = 0;
        GLint valueMask = 0;
        GLint fail = GL_KEEP;
        GLint depthFail = GL_KEEP;
        GLint depthPass = GL_KEEP;
        GLint writeMask = 0;
    };

    // バインド
    GLint vertexArray = 0;
    GLint arrayBuffer = 0;
    GLint program = 0;
    GLint drawFramebuffer = 0;
    GLint readFramebuffer = 0;
    GLint renderbuffer = 0;
    GLint activeTexture = GL_TEXTURE0;
    std::vector<TextureUnit> textureUnits;

    // 有効/無効
    GLboolean depthTest = GL_FALSE;
    GLboolean stencilTest = GL_FALSE;
    GLboolean blend = GL_FALSE;
    GLboolean cullFace = GL_FALSE;
    GLboolean scissorTest = GL_FALSE;
    GLboolean polygonOffsetFill = GL_FALSE;
    GLboolean sampleAlphaToCoverage = GL_FALSE;
    GLboolean dither = GL_FALSE;
    GLboolean multisample = GL_FALSE;
    GLboolean programPointSize = GL_FALSE;
    GLboolean cubeMapSeamless = GL_FALSE;

    // 固定機能の値
    GLint depthFunc = GL_LESS;
    GLboolean depthWriteMask = GL_TRUE;
    StencilFace stencilFront;
    StencilFace stencilBack;
    GLint blendSrcRgb = GL_ONE;
    GLint blendDstRgb = GL_ZERO;
    GLint blendSrcAlpha = GL_ONE;
    GLint blendDstAlpha = GL_ZERO;
    GLint blendEquationRgb = GL_FUNC_ADD;
    GLint blendEquationAlpha = GL_FUNC_ADD;
    GLfloat blendColor[4] = {};
    GLboolean colorWriteMask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
    GLfloat polygonOffsetFactor = 0.0f;
    GLfloat polygonOffsetUnits = 0.0f;
    GLint cullFaceMode = GL_BACK;
    GLint frontFace = GL_CCW;
    GLint viewport[4] = {};
    GLint scissorBox[4] = {};
    GLint unpackAlignment = 4;
    GLint packAlignment = 4;
};

namespace {

bool g_valid = false;

// sokol がバインドを外すテクスチャユニットの数（_sg_gl_cache_clear_texture_sampler_bindings と同じ上限）
int sokolTextureUnitCount() {
    GLint maxUnits = 0;
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxUnits);
    return std::clamp(static_cast<int>(maxUnits), 0, static_cast<int>(SG_MAX_TEXTURE_SAMPLER_PAIRS));
}

void captureBindings(GlStateSnapshot& s) {
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &s.vertexArray);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &s.arrayBuffer);
    glGetIntegerv(GL_CURRENT_PROGRAM, &s.program);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &s.drawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &s.readFramebuffer);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &s.renderbuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &s.activeTexture);

    s.textureUnits.resize(static_cast<size_t>(sokolTextureUnitCount()));
    for (size_t i = 0; i < s.textureUnits.size(); ++i) {
        GlStateSnapshot::TextureUnit& unit = s.textureUnits[i];
        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + i));
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &unit.tex2D);
        glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &unit.texCube);
        glGetIntegerv(GL_TEXTURE_BINDING_3D, &unit.tex3D);
        glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &unit.tex2DArray);
        glGetIntegerv(GL_SAMPLER_BINDING, &unit.sampler);
    }
    glActiveTexture(static_cast<GLenum>(s.activeTexture));
}

void captureCapabilities(GlStateSnapshot& s) {
    s.depthTest = glIsEnabled(GL_DEPTH_TEST);
    s.stencilTest = glIsEnabled(GL_STENCIL_TEST);
    s.blend = glIsEnabled(GL_BLEND);
    s.cullFace = glIsEnabled(GL_CULL_FACE);
    s.scissorTest = glIsEnabled(GL_SCISSOR_TEST);
    s.polygonOffsetFill = glIsEnabled(GL_POLYGON_OFFSET_FILL);
    s.sampleAlphaToCoverage = glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE);
    s.dither = glIsEnabled(GL_DITHER);
    s.multisample = glIsEnabled(GL_MULTISAMPLE);
    s.programPointSize = glIsEnabled(GL_PROGRAM_POINT_SIZE);
    s.cubeMapSeamless = glIsEnabled(GL_TEXTURE_CUBE_MAP_SEAMLESS);
}

void captureStencilFace(GlStateSnapshot::StencilFace& f, bool back) {
    glGetIntegerv(back ? GL_STENCIL_BACK_FUNC : GL_STENCIL_FUNC, &f.func);
    glGetIntegerv(back ? GL_STENCIL_BACK_REF : GL_STENCIL_REF, &f.ref);
    glGetIntegerv(back ? GL_STENCIL_BACK_VALUE_MASK : GL_STENCIL_VALUE_MASK, &f.valueMask);
    glGetIntegerv(back ? GL_STENCIL_BACK_FAIL : GL_STENCIL_FAIL, &f.fail);
    glGetIntegerv(back ? GL_STENCIL_BACK_PASS_DEPTH_FAIL : GL_STENCIL_PASS_DEPTH_FAIL, &f.depthFail);
    glGetIntegerv(back ? GL_STENCIL_BACK_PASS_DEPTH_PASS : GL_STENCIL_PASS_DEPTH_PASS, &f.depthPass);
    glGetIntegerv(back ? GL_STENCIL_BACK_WRITEMASK : GL_STENCIL_WRITEMASK, &f.writeMask);
}

void captureFixedFunction(GlStateSnapshot& s) {
    glGetIntegerv(GL_DEPTH_FUNC, &s.depthFunc);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &s.depthWriteMask);
    captureStencilFace(s.stencilFront, false);
    captureStencilFace(s.stencilBack, true);
    glGetIntegerv(GL_BLEND_SRC_RGB, &s.blendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &s.blendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &s.blendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &s.blendDstAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &s.blendEquationRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &s.blendEquationAlpha);
    glGetFloatv(GL_BLEND_COLOR, s.blendColor);
    glGetBooleanv(GL_COLOR_WRITEMASK, s.colorWriteMask);
    glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &s.polygonOffsetFactor);
    glGetFloatv(GL_POLYGON_OFFSET_UNITS, &s.polygonOffsetUnits);
    glGetIntegerv(GL_CULL_FACE_MODE, &s.cullFaceMode);
    glGetIntegerv(GL_FRONT_FACE, &s.frontFace);
    glGetIntegerv(GL_VIEWPORT, s.viewport);
    glGetIntegerv(GL_SCISSOR_BOX, s.scissorBox);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &s.unpackAlignment);
    glGetIntegerv(GL_PACK_ALIGNMENT, &s.packAlignment);
}

void captureGlState(GlStateSnapshot& s) {
    captureBindings(s);
    captureCapabilities(s);
    captureFixedFunction(s);
}

void setCapability(GLenum cap, GLboolean enabled) {
    if (enabled) glEnable(cap);
    else glDisable(cap);
}

void restoreBindings(const GlStateSnapshot& s) {
    for (size_t i = 0; i < s.textureUnits.size(); ++i) {
        const GlStateSnapshot::TextureUnit& unit = s.textureUnits[i];
        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + i));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(unit.tex2D));
        glBindTexture(GL_TEXTURE_CUBE_MAP, static_cast<GLuint>(unit.texCube));
        glBindTexture(GL_TEXTURE_3D, static_cast<GLuint>(unit.tex3D));
        glBindTexture(GL_TEXTURE_2D_ARRAY, static_cast<GLuint>(unit.tex2DArray));
        glBindSampler(static_cast<GLuint>(i), static_cast<GLuint>(unit.sampler));
    }
    glActiveTexture(static_cast<GLenum>(s.activeTexture));

    // ELEMENT_ARRAY_BUFFER は VAO の状態なので、VAO を戻せば一緒に戻る
    glBindVertexArray(static_cast<GLuint>(s.vertexArray));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(s.arrayBuffer));
    glUseProgram(static_cast<GLuint>(s.program));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(s.drawFramebuffer));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(s.readFramebuffer));
    glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(s.renderbuffer));
}

void restoreCapabilities(const GlStateSnapshot& s) {
    setCapability(GL_DEPTH_TEST, s.depthTest);
    setCapability(GL_STENCIL_TEST, s.stencilTest);
    setCapability(GL_BLEND, s.blend);
    setCapability(GL_CULL_FACE, s.cullFace);
    setCapability(GL_SCISSOR_TEST, s.scissorTest);
    setCapability(GL_POLYGON_OFFSET_FILL, s.polygonOffsetFill);
    setCapability(GL_SAMPLE_ALPHA_TO_COVERAGE, s.sampleAlphaToCoverage);
    setCapability(GL_DITHER, s.dither);
    setCapability(GL_MULTISAMPLE, s.multisample);
    setCapability(GL_PROGRAM_POINT_SIZE, s.programPointSize);
    setCapability(GL_TEXTURE_CUBE_MAP_SEAMLESS, s.cubeMapSeamless);
}

void restoreStencilFace(const GlStateSnapshot::StencilFace& f, GLenum face) {
    glStencilFuncSeparate(face, static_cast<GLenum>(f.func), f.ref, static_cast<GLuint>(f.valueMask));
    glStencilOpSeparate(face, static_cast<GLenum>(f.fail), static_cast<GLenum>(f.depthFail),
                        static_cast<GLenum>(f.depthPass));
    glStencilMaskSeparate(face, static_cast<GLuint>(f.writeMask));
}

void restoreFixedFunction(const GlStateSnapshot& s) {
    glDepthFunc(static_cast<GLenum>(s.depthFunc));
    glDepthMask(s.depthWriteMask);
    restoreStencilFace(s.stencilFront, GL_FRONT);
    restoreStencilFace(s.stencilBack, GL_BACK);
    glBlendFuncSeparate(static_cast<GLenum>(s.blendSrcRgb), static_cast<GLenum>(s.blendDstRgb),
                        static_cast<GLenum>(s.blendSrcAlpha), static_cast<GLenum>(s.blendDstAlpha));
    glBlendEquationSeparate(static_cast<GLenum>(s.blendEquationRgb), static_cast<GLenum>(s.blendEquationAlpha));
    glBlendColor(s.blendColor[0], s.blendColor[1], s.blendColor[2], s.blendColor[3]);
    glColorMask(s.colorWriteMask[0], s.colorWriteMask[1], s.colorWriteMask[2], s.colorWriteMask[3]);
    glPolygonOffset(s.polygonOffsetFactor, s.polygonOffsetUnits);
    glCullFace(static_cast<GLenum>(s.cullFaceMode));
    glFrontFace(static_cast<GLenum>(s.frontFace));
    glViewport(s.viewport[0], s.viewport[1], s.viewport[2], s.viewport[3]);
    glScissor(s.scissorBox[0], s.scissorBox[1], s.scissorBox[2], s.scissorBox[3]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, s.unpackAlignment);
    glPixelStorei(GL_PACK_ALIGNMENT, s.packAlignment);
}

void restoreGlState(const GlStateSnapshot& s) {
    restoreBindings(s);
    restoreCapabilities(s);
    restoreFixedFunction(s);
}

void logFromSokol(const char* tag, uint32_t logLevel, uint32_t logItemId, const char* message,
                  uint32_t lineNr, const char* fileName, void* userData) {
    (void)tag;
    (void)fileName;
    (void)userData;
    const char* text = message ? message : "(no message in release build)";
    switch (logLevel) {
        case 0:  RCBN_ERROR("sokol_gfx PANIC (item " << logItemId << ", line " << lineNr << "): " << text); break;
        case 1:  RCBN_ERROR("sokol_gfx error (item " << logItemId << ", line " << lineNr << "): " << text); break;
        case 2:  RCBN_WARN("sokol_gfx warning (item " << logItemId << ", line " << lineNr << "): " << text); break;
        default: RCBN_LOG("sokol_gfx (item " << logItemId << "): " << text); break;
    }
}

const char* backendName(sg_backend backend) {
    switch (backend) {
        case SG_BACKEND_GLCORE:      return "GLCORE";
        case SG_BACKEND_GLES3:       return "GLES3";
        case SG_BACKEND_D3D11:       return "D3D11";
        case SG_BACKEND_METAL_MACOS: return "METAL_MACOS";
        case SG_BACKEND_DUMMY:       return "DUMMY";
        default:                     return "other";
    }
}

} // namespace

bool GfxDevice::setup() {
    if (g_valid) return true;

    // sg_setup は GL 状態を sokol 既定値へ書き換える（シザー有効・FrontFace=CW・深度 ALWAYS など）。
    // 既存の生 GL 描画が影響を受けないよう、前後で保存・復元する。
    GlStateSnapshot saved;
    captureGlState(saved);

    sg_desc desc = {};
    desc.logger.func = logFromSokol;
    sg_setup(&desc);

    restoreGlState(saved);

    if (!sg_isvalid()) {
        RCBN_ERROR("sokol_gfx setup failed");
        return false;
    }
    g_valid = true;
    RCBN_LOG("sokol_gfx initialized (backend: " << backendName(sg_query_backend()) << ")");
    return true;
}

void GfxDevice::shutdown() {
    if (!g_valid) return;
    GlStateSnapshot saved;
    captureGlState(saved);
    sg_shutdown();
    restoreGlState(saved);
    g_valid = false;
}

bool GfxDevice::isValid() {
    return g_valid;
}

void GfxDevice::commit() {
    if (!g_valid) return;
    sg_commit();
}

GfxDevice::SokolSection::SokolSection() {
    if (!g_valid) return;
    m_saved = std::make_unique<GlStateSnapshot>();
    captureGlState(*m_saved);
    sg_reset_state_cache();
}

GfxDevice::SokolSection::~SokolSection() {
    if (!m_saved) return;
    // sokol の VAO が束縛されている間にバインドを強制解除し、キャッシュを空にしてから生 GL 状態へ戻す。
    // こうしておくと、区間の外で呼ぶ sg_commit() が生 GL 側の VAO の要素バッファを外すことがない。
    sg_reset_state_cache();
    restoreGlState(*m_saved);
}
