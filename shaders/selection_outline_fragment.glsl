#version 330 core
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D selectionMask;
uniform vec2 texelSize;
uniform float outlineWidth;
uniform vec4 outlineColor;
void main() {
    float center = texture(selectionMask, TexCoord).r;
    vec2 stepSize = texelSize * max(outlineWidth, 1.0);
    float surrounding = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            if (x == 0 && y == 0) continue;
            surrounding = max(surrounding, texture(selectionMask, TexCoord + vec2(x, y) * stepSize).r);
        }
    }
    float outline = surrounding * (1.0 - center);
    FragColor = vec4(outlineColor.rgb, outlineColor.a * outline);
}
