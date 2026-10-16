#include <Core/ShdcProgram.hpp>
#include <Util/Logger.hpp>
#include <GL/glew.h>
#include <sokol/sokol_gfx.h>

static_assert(ShdcProgram::MAX_UNIFORM_BLOCKS == SG_MAX_UNIFORMBLOCK_BINDSLOTS);
static_assert(ShdcProgram::MAX_TEXTURE_SAMPLER_PAIRS == SG_MAX_TEXTURE_SAMPLER_PAIRS);

namespace {

GLuint compileStage(GLenum type, const char* source, const std::string& label, const char* stageName) {
    if (!source || source[0] == '\0') {
        RCBN_ERROR("sokol-shdc shader has no GLSL source (" << label << " " << stageName << ")");
        return 0;
    }
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        RCBN_ERROR("sokol-shdc shader compilation failed (" << label << " " << stageName << "): " << log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint linkProgram(GLuint vertex, GLuint fragment, const std::string& label) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        RCBN_ERROR("sokol-shdc shader link failed (" << label << "): " << log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

} // namespace

ShdcProgram::ShdcProgram() {
    m_pairViewSlot.fill(-1);
}

ShdcProgram::~ShdcProgram() {
    destroy();
}

bool ShdcProgram::create(const sg_shader_desc& desc, const char* label) {
    destroy();
    m_label = label ? label : "";

    GLuint vertex = compileStage(GL_VERTEX_SHADER, desc.vertex_func.source, m_label, "vertex");
    GLuint fragment = compileStage(GL_FRAGMENT_SHADER, desc.fragment_func.source, m_label, "fragment");
    if (vertex && fragment) m_program = linkProgram(vertex, fragment, m_label);
    if (vertex) glDeleteShader(vertex);
    if (fragment) glDeleteShader(fragment);
    if (!m_program) return false;

    if (!cacheUniformBlocks(desc) || !assignTextureUnits(desc)) {
        destroy();
        return false;
    }
    return true;
}

void ShdcProgram::destroy() {
    if (m_program) glDeleteProgram(m_program);
    m_program = 0;
    m_uniformBlocks.fill(UniformBlock{});
    m_pairViewSlot.fill(-1);
}

bool ShdcProgram::isValid() const {
    return m_program != 0;
}

unsigned int ShdcProgram::program() const {
    return m_program;
}

void ShdcProgram::use() const {
    glUseProgram(m_program);
}

// sokol-shdc は GL 向けに各ユニフォームブロックを「uniform vec4 <ブロック名>[N];」へ平坦化する。
// 平坦化されないブロック（int 等を含む）は未対応なので、float/vec/mat だけのブロックに分けること。
bool ShdcProgram::cacheUniformBlocks(const sg_shader_desc& desc) {
    for (int slot = 0; slot < MAX_UNIFORM_BLOCKS; ++slot) {
        const sg_shader_uniform_block& block = desc.uniform_blocks[slot];
        if (block.stage == SG_SHADERSTAGE_NONE) continue;
        const sg_glsl_shader_uniform& flat = block.glsl_uniforms[0];
        if (flat.type != SG_UNIFORMTYPE_FLOAT4 || !flat.glsl_name || block.glsl_uniforms[1].type != SG_UNIFORMTYPE_INVALID) {
            RCBN_ERROR("sokol-shdc uniform block " << slot << " is not a flattened vec4 array (" << m_label
                       << "); only float/vec/mat members are supported");
            return false;
        }
        UniformBlock& cached = m_uniformBlocks[static_cast<size_t>(slot)];
        cached.location = glGetUniformLocation(m_program, flat.glsl_name);
        cached.vec4Count = flat.array_count;
        cached.size = static_cast<std::size_t>(block.size);
        // 使われないブロックはドライバに消されて -1 になる。エラーではない（applyUniforms で無視される）。
    }
    return true;
}

bool ShdcProgram::assignTextureUnits(const sg_shader_desc& desc) {
    GLint previousProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glUseProgram(m_program);
    for (int pair = 0; pair < MAX_TEXTURE_SAMPLER_PAIRS; ++pair) {
        const sg_shader_texture_sampler_pair& p = desc.texture_sampler_pairs[pair];
        if (p.stage == SG_SHADERSTAGE_NONE) continue;
        if (!p.glsl_name) {
            RCBN_ERROR("sokol-shdc texture-sampler pair " << pair << " has no GLSL name (" << m_label << ")");
            glUseProgram(static_cast<GLuint>(previousProgram));
            return false;
        }
        m_pairViewSlot[static_cast<size_t>(pair)] = p.view_slot;
        const GLint location = glGetUniformLocation(m_program, p.glsl_name);
        if (location >= 0) glUniform1i(location, pair);
    }
    glUseProgram(static_cast<GLuint>(previousProgram));
    return true;
}

void ShdcProgram::applyUniforms(int blockSlot, const void* data, std::size_t size) const {
    if (blockSlot < 0 || blockSlot >= MAX_UNIFORM_BLOCKS) {
        RCBN_ERROR("applyUniforms: invalid block slot " << blockSlot << " (" << m_label << ")");
        return;
    }
    const UniformBlock& block = m_uniformBlocks[static_cast<size_t>(blockSlot)];
    if (block.size == 0) {
        RCBN_ERROR("applyUniforms: block slot " << blockSlot << " is not declared (" << m_label << ")");
        return;
    }
    if (size != block.size) {
        RCBN_ERROR("applyUniforms: size mismatch for block " << blockSlot << " (" << m_label << "): got "
                   << size << ", shader expects " << block.size);
        return;
    }
    if (block.location < 0) return;
    glUniform4fv(block.location, block.vec4Count, static_cast<const GLfloat*>(data));
}

void ShdcProgram::bindTexture(int viewSlot, unsigned int target, unsigned int texture) const {
    bool bound = false;
    for (int pair = 0; pair < MAX_TEXTURE_SAMPLER_PAIRS; ++pair) {
        if (m_pairViewSlot[static_cast<size_t>(pair)] != viewSlot) continue;
        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + pair));
        glBindTexture(static_cast<GLenum>(target), static_cast<GLuint>(texture));
        bound = true;
    }
    if (!bound) {
        RCBN_ERROR("bindTexture: view slot " << viewSlot << " is not used by any texture-sampler pair (" << m_label << ")");
    }
}
