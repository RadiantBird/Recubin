#include <Core/PostEffectSpirv.hpp>

#include <Core/PostEffectGlsl.hpp>

#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>

#include <algorithm>
#include <charconv>
#include <memory>
#include <mutex>
#include <string>

namespace PostEffectSpirv {
namespace {

struct CompilerProcess {
    const bool initialized = glslang_initialize_process() != 0;
    std::mutex mutex;

    ~CompilerProcess() {
        if (initialized) {
            glslang_finalize_process();
        }
    }
};

std::size_t diagnosticLine(std::string_view log) {
    constexpr std::string_view prefix = "ERROR: ";
    while (!log.empty()) {
        const auto end = log.find('\n');
        const auto entry = log.substr(0, end);
        if (entry.starts_with(prefix)) {
            const auto fields = entry.substr(prefix.size());
            const auto sourceEnd = fields.find(':');
            if (sourceEnd != std::string_view::npos) {
                const auto location = fields.substr(sourceEnd + 1);
                std::size_t number = 0;
                const auto [next, error] = std::from_chars(location.data(),
                                                          location.data() + location.size(), number);
                if (error == std::errc{} && next != location.data() + location.size() && *next == ':') {
                    return number;
                }
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        log.remove_prefix(end + 1);
    }
    return 0;
}

CompileResult failure(std::string_view filename, std::size_t line, std::string_view message) {
    CompileResult result;
    result.diagnostic.line = line;
    result.diagnostic.message = std::string(filename);
    if (result.diagnostic.line != 0) {
        result.diagnostic.message += ':' + std::to_string(result.diagnostic.line);
    }
    result.diagnostic.message += ": ";
    result.diagnostic.message += message;
    return result;
}

std::string_view firstLog(const char* primary, const char* secondary, std::string_view fallback) {
    if (primary != nullptr && *primary != '\0') {
        return primary;
    }
    if (secondary != nullptr && *secondary != '\0') {
        return secondary;
    }
    return fallback;
}

} // namespace

CompileResult compile(std::string_view source, std::string_view filename) {
    const std::string displayName = filename.empty() ? std::string("<PostEffect>") : std::string(filename);

    const std::size_t nul = source.find('\0');
    if (nul != std::string_view::npos) {
        std::size_t line = 1;
        line += static_cast<std::size_t>(std::count(source.begin(), source.begin() + nul, '\n'));
        return failure(displayName, line, "embedded NUL at byte " + std::to_string(nul));
    }

    const PostEffectGlsl::NormalizeResult normalized = PostEffectGlsl::normalize(source);
    if (!normalized.success) {
        return failure(displayName, normalized.line, normalized.message);
    }

    static CompilerProcess process;
    if (!process.initialized) {
        return failure(displayName, 0, "glslang_initialize_process failed");
    }
    std::lock_guard compileLock(process.mutex);

    glslang_input_t input{};
    input.language = GLSLANG_SOURCE_GLSL;
    input.stage = GLSLANG_STAGE_FRAGMENT;
    input.client = GLSLANG_CLIENT_VULKAN;
    input.client_version = GLSLANG_TARGET_VULKAN_1_0;
    input.target_language = GLSLANG_TARGET_SPV;
    input.target_language_version = GLSLANG_TARGET_SPV_1_0;
    input.code = normalized.source.c_str();
    input.default_version = 450;
    input.default_profile = GLSLANG_CORE_PROFILE;
    input.force_default_version_and_profile = 0;
    input.forward_compatible = 0;
    input.messages = static_cast<glslang_messages_t>(GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT);
    input.resource = glslang_default_resource();

    std::unique_ptr<glslang_shader_t, decltype(&glslang_shader_delete)>
        shader(glslang_shader_create(&input), glslang_shader_delete);
    if (!shader) {
        return failure(displayName, 0, "glslang_shader_create failed");
    }

    if (!glslang_shader_preprocess(shader.get(), &input) || !glslang_shader_parse(shader.get(), &input)) {
        const auto log = firstLog(glslang_shader_get_info_log(shader.get()),
                                  glslang_shader_get_info_debug_log(shader.get()), "GLSL compilation failed");
        return failure(displayName, diagnosticLine(log), log);
    }

    std::unique_ptr<glslang_program_t, decltype(&glslang_program_delete)>
        program(glslang_program_create(), glslang_program_delete);
    if (!program) {
        return failure(displayName, 0, "glslang_program_create failed");
    }
    glslang_program_add_shader(program.get(), shader.get());
    if (!glslang_program_link(program.get(), input.messages)) {
        const auto log = firstLog(glslang_program_get_info_log(program.get()),
                                  glslang_program_get_info_debug_log(program.get()), "GLSL linking failed");
        return failure(displayName, diagnosticLine(log), log);
    }

    glslang_program_SPIRV_generate(program.get(), GLSLANG_STAGE_FRAGMENT);
    const std::size_t wordCount = glslang_program_SPIRV_get_size(program.get());
    if (wordCount == 0) {
        const auto log = firstLog(glslang_program_SPIRV_get_messages(program.get()), nullptr,
                                  "SPIR-V generation produced no words");
        return failure(displayName, diagnosticLine(log), log);
    }

    CompileResult result;
    result.success = true;
    result.spirv.resize(wordCount);
    glslang_program_SPIRV_get(program.get(), result.spirv.data());
    return result;
}

} // namespace PostEffectSpirv
