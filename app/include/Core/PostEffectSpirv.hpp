#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace PostEffectSpirv {

struct Diagnostic {
    std::size_t line = 0;
    std::string message;
};

struct CompileResult {
    bool success = false;
    std::vector<std::uint32_t> spirv;
    Diagnostic diagnostic;
};

/**
 * Normalize and compile a public PostEffect GLSL shader to Vulkan SPIR-V 1.0.
 * The filename is retained in diagnostics and is never used to access the
 * filesystem. This function accepts source text only.
 */
CompileResult compile(std::string_view source, std::string_view filename);

} // namespace PostEffectSpirv
