#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace PostEffectGlsl {

struct NormalizeResult {
    bool success = false;
    std::string source;
    std::size_t line = 0;
    std::string message;
};

/**
 * Normalize the public GLSL 330 PostEffect interface for SDL GPU shader
 * compilation. The function only rewrites the declared interface. The user
 * shader body remains GLSL and is responsible for its own coordinate logic.
 */
NormalizeResult normalize(std::string_view source);

} // namespace PostEffectGlsl
