// 雲（Weather）。カメラ追従の水平クアッドにノイズテクスチャをスクロールして貼る。
// m_cloudVAO: location 0 = 位置(vec3), 1 = UV(vec2)
@module cloud

@vs cloud_vs
layout(binding = 0) uniform vs_params {
    mat4 view;
    mat4 projection;
};
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = projection * view * vec4(aPos, 1.0);
}
@end

@fs cloud_fs
layout(binding = 0) uniform texture2D cloudTex;
layout(binding = 0) uniform sampler cloudTexSampler;
layout(binding = 1) uniform fs_params {
    vec4  cloudColor;
    vec2  windOffset;
    float cloudCover;
    float cloudDensity;
};
in vec2 vUV;
out vec4 FragColor;
void main() {
    float n = texture(sampler2D(cloudTex, cloudTexSampler), vUV - windOffset).r;
    float alpha = smoothstep(1.0 - cloudCover, 1.0, n) * cloudDensity * cloudColor.a;
    FragColor = vec4(cloudColor.rgb, alpha);
}
@end

@program draw cloud_vs cloud_fs
