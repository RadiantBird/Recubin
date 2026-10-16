// パーティクル（ビルボード・テクスチャなし単色頂点）
// m_particleVAO: location 0 = 位置(vec3), 1 = 色(vec4)
@module particle

@vs particle_vs
layout(binding = 0) uniform vs_params {
    mat4 view;
    mat4 projection;
};
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = projection * view * vec4(aPos, 1.0);
}
@end

@fs particle_fs
in vec4 vColor;
out vec4 FragColor;
void main() {
    FragColor = vColor;
}
@end

@program draw particle_vs particle_fs
