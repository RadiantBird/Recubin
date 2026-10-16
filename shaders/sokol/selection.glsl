// エディターの選択アウトライン（Renderer::renderSelectionOutline）
// 1. mask: 選択中の形状をマスク用 FBO に 1.0 で描く
// 2. outline: マスクの周囲 8 近傍を見て、マスクの外側だけに輪郭色を出す
@module selection
@include _fullscreen.glsl

@vs mask_vs
layout(binding = 0) uniform mask_vs_params {
    mat4 model;
    mat4 view;
    mat4 projection;
};
layout(location = 0) in vec3 aPos;

void main() { gl_Position = projection * view * model * vec4(aPos, 1.0); }
@end

@fs mask_fs
layout(location = 0) out float mask;

void main() { mask = 1.0; }
@end

@program mask mask_vs mask_fs

@vs outline_vs
@include_block fullscreen_vs
@end

@fs outline_fs
layout(binding = 0) uniform texture2D selectionMask;
layout(binding = 0) uniform sampler selectionMaskSampler;
layout(binding = 0) uniform outline_fs_params {
    vec2 texelSize;
    float outlineWidth;
    vec4 outlineColor;
};

in vec2 TexCoord;
out vec4 FragColor;

void main() {
    float center = texture(sampler2D(selectionMask, selectionMaskSampler), TexCoord).r;
    vec2 stepSize = texelSize * max(outlineWidth, 1.0);
    float surrounding = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            if (x == 0 && y == 0) continue;
            surrounding = max(surrounding, texture(sampler2D(selectionMask, selectionMaskSampler), TexCoord + vec2(x, y) * stepSize).r);
        }
    }
    float outline = surrounding * (1.0 - center);
    FragColor = vec4(outlineColor.rgb, outlineColor.a * outline);
}
@end

@program outline outline_vs outline_fs
