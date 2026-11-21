#include <Core/PostEffectSpirv.hpp>
#include <Core/SdlGpuWindow.hpp>

#include <SDL3/SDL_main.h>

#include <iostream>
#include <string>
#include <string_view>

int main(int argc, char** argv) {
    const std::string source = R"GLSL(
#version 330 core
uniform sampler2D screenTexture;
uniform vec2 u_resolution;
uniform float u_time;
in vec2 TexCoord;
out vec4 FragColor;
void main() {
    FragColor = texture(screenTexture, TexCoord) + vec4(u_time / max(u_resolution.x, 1.0));
}
)GLSL";

    const auto result = PostEffectSpirv::compile(source, "post/check.glsl");
    if (!result.success || result.spirv.size() < 5 || result.spirv.front() != 0x07230203u) {
        std::cerr << "valid shader did not produce SPIR-V: " << result.diagnostic.message << '\n';
        return 1;
    }

    const std::string nulSource = source.substr(0, source.size() / 2) + '\0' + source.substr(source.size() / 2);
    const auto nulResult = PostEffectSpirv::compile(nulSource, "post/nul.glsl");
    if (nulResult.success || nulResult.diagnostic.message.find("embedded NUL") == std::string::npos) {
        std::cerr << "embedded NUL was not rejected\n";
        return 1;
    }

    const auto invalidResult = PostEffectSpirv::compile(
        "#version 330 core\n"
        "out vec4 FragColor;\n"
        "void main() {\n"
        "    FragColor = undefinedName;\n"
        "}\n", "post/invalid.glsl");
    if (invalidResult.success || invalidResult.diagnostic.line != 4 ||
        invalidResult.diagnostic.message.find("post/invalid.glsl:4:") == std::string::npos) {
        std::cerr << "invalid shader did not retain its filename and line: "
                  << invalidResult.diagnostic.message << '\n';
        return 1;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--vulkan-device") {
        SdlGpuWindow host;
        SdlGpuWindowConfig config;
        config.present = false;
        config.driver = SdlGpuDriver::Vulkan;
        config.shaderFormats = SDL_GPU_SHADERFORMAT_SPIRV;
        std::string error;
        if (!host.initialize(config, &error)) {
            std::cerr << error << '\n';
            return 1;
        }
        SDL_GPUShaderCreateInfo shaderInfo{};
        shaderInfo.code = reinterpret_cast<const Uint8*>(result.spirv.data());
        shaderInfo.code_size = result.spirv.size() * sizeof(result.spirv.front());
        shaderInfo.entrypoint = "main";
        shaderInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        shaderInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        shaderInfo.num_samplers = 1;
        shaderInfo.num_uniform_buffers = 1;
        SDL_GPUShader* shader = SDL_CreateGPUShader(host.device(), &shaderInfo);
        if (!shader) {
            std::cerr << "Vulkan failed to create the compiled PostEffect shader: " << SDL_GetError() << '\n';
            return 1;
        }
        SDL_ReleaseGPUShader(host.device(), shader);
    }
    std::cout << "GLSL compilation and source diagnostics verified\n";
    return 0;
}
