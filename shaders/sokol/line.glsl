// 補助線（制約・物理デバッグ・雷・ハイライト縁取りなど）。リボン化した三角形を単色で描く。
// m_lineVAO: location 0 = 位置(vec3)
@module line

@vs line_vs
layout(binding = 0) uniform vs_params {
    mat4 view;
    mat4 projection;
};
layout(location = 0) in vec3 aPos;
void main() {
    gl_Position = projection * view * vec4(aPos, 1.0);
}
@end

@fs line_fs
layout(binding = 1) uniform fs_params {
    vec4 lineColor;
};
out vec4 FragColor;
void main() {
    FragColor = lineColor;
}
@end

@program draw line_vs line_fs
