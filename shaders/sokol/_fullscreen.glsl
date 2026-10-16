// 全画面クアッド用の頂点シェーダー本体（Renderer::m_postVAO: location 0 = 位置, 1 = UV）。
// 使う側は @vs の中で @include_block fullscreen_vs と書く。
@block fullscreen_vs
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

void main() {
    TexCoord = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
@end
