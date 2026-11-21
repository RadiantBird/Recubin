#include <Core/PostEffectGlsl.hpp>

#include <cassert>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    const std::string valid =
        "// header\r\n"
        "# version/* keep comments legal */ 330\tcore\r\n"
        "uniform sampler2D screenTexture;\r\n"
        "in vec2 TexCoord;\r\n"
        "out vec4 FragColor;\r\n"
        "void main() { FragColor = texture(screenTexture, TexCoord); }\r\n";
    const auto normalized = PostEffectGlsl::normalize(valid);
    assert(normalized.success);
    assert(normalized.source.find("#version 450") != std::string::npos);
    assert(normalized.source.find("layout(set = 2, binding = 0)") != std::string::npos);
    assert(normalized.source.find("#line 1") != std::string::npos);
    if (argc == 2) {
        std::ofstream output(argv[1], std::ios::binary);
        output << normalized.source;
        output.close();
        if (!output) {
            return 1;
        }
    }

    const auto invalid = PostEffectGlsl::normalize(
        "#version 330 core\n"
        "uniform vec3 u_time;\n"
        "out vec4 FragColor;\n");
    assert(!invalid.success);
    assert(invalid.line == 2);

    const auto local = PostEffectGlsl::normalize(
        "#version 330 core\n"
        "out vec4 FragColor;\n"
        "void helper(float u_time) { if (u_time > 0.0) { float u_param1 = u_time; } }\n"
        "void main() { float u_time = 0.0; FragColor = vec4(u_time); }\n");
    assert(local.success);
    assert(local.source.find("float u_time = 0.0") != std::string::npos);

    const auto layout = PostEffectGlsl::normalize(
        "#version 330 core\n"
        "layout(location = 0) out vec4 FragColor;\n");
    assert(!layout.success);
    assert(layout.line == 2);
}
