// 組み込みポストエフェクト（Renderer::renderPostEffects）
// カスタム PostEffect はユーザーの GLSL をそのまま使うので、この形式ではなく shaders/postprocess_vertex.glsl と組む。
@module post
@include _fullscreen.glsl

@vs post_vs
@include_block fullscreen_vs
@end

@fs post_fs
layout(binding = 0) uniform texture2D screenTexture;
layout(binding = 0) uniform sampler screenTextureSampler;
layout(binding = 0) uniform fs_params {
    vec2  u_resolution;
    float u_effectType; // 0=None 1=CRT 2=Posterization 3=Pixelize 4=Saturation 5=VHS 6=ChromaticAberration（整数値）
    float u_intensity;  // 元画像とのブレンド比率 (0..1)
    float u_param1;     // ScanlineCount(CRT) / Levels(Posterization) / PixelSize(Pixelize) / SaturationAmount(Saturation) / NoiseAmount(VHS) / Offset(ChromaticAberration)
    float u_param2;     // CurveAmount(CRT)。他タイプでは未使用
    float u_time;       // 経過時間（秒）。VHSのノイズに使用
};

in vec2 TexCoord;
out vec4 FragColor;

vec4 sampleScreen(vec2 uv) {
    return texture(sampler2D(screenTexture, screenTextureSampler), uv);
}

vec3 applyCRT(vec2 uv) {
    // バレル歪み（簡易）
    vec2 centered = uv * 2.0 - 1.0;
    float dist = dot(centered, centered);
    vec2 distortedUv = (centered * (1.0 + u_param2 * dist)) * 0.5 + 0.5;

    // 0.0 <= distortedUv なら 1.0、そうでなければ 0.0
    vec2 isAboveMin = step(vec2(0.0), distortedUv);
    // distortedUv <= 1.0 なら 1.0、そうでなければ 0.0
    vec2 isBelowMax = step(distortedUv, vec2(1.0));

    // 4つの条件（Xの上下限、Yの上下限）をすべて掛け合わせる
    // どこか1つでも画面外（0.0）があれば、maskは 0.0 になる
    float mask = isAboveMin.x * isAboveMin.y * isBelowMax.x * isBelowMax.y;

    vec3 col = sampleScreen(distortedUv).rgb;

    float scanlines = max(u_param1, 1.0);
    float scan = sin(distortedUv.y * scanlines * 3.14159265);
    col *= 1.0 - 0.25 * (0.5 - 0.5 * scan);

    return col * mask;
}

vec3 applyPosterize(vec3 col) {
    float levels = max(u_param1, 1.0);
    return floor(col * levels + 0.5) / levels;
}

vec3 applyPixelize(vec2 uv) {
    float pixelSize = max(u_param1, 1.0);
    vec2 cells = max(u_resolution / pixelSize, vec2(1.0));
    vec2 snapped = (floor(uv * cells) + 0.5) / cells;
    return sampleScreen(snapped).rgb;
}

vec3 applySaturation(vec3 col) {
    float gray = dot(col, vec3(0.299, 0.587, 0.114));
    return mix(vec3(gray), col, u_param1);
}

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

vec3 applyVHS(vec2 uv) {
    float noiseAmount = max(u_param1, 0.0);

    // 走査線ごとの横ズレ
    float lineNoise = hash(vec2(floor(uv.y * u_resolution.y), u_time)) - 0.5;
    vec2 jitteredUv = uv + vec2(lineNoise * noiseAmount * 0.02, 0.0);

    vec3 col = sampleScreen(jitteredUv).rgb;

    // 粒状ノイズ
    float grain = hash(uv * u_resolution + u_time) - 0.5;
    col += grain * noiseAmount * 0.25;

    return col;
}

vec3 applyChromaticAberration(vec2 uv) {
    vec2 dir = uv - 0.5;
    vec2 offset = dir * u_param1 * 0.02;

    float r = sampleScreen(uv + offset).r;
    float g = sampleScreen(uv).g;
    float b = sampleScreen(uv - offset).b;

    return vec3(r, g, b);
}

void main() {
    vec3 original = sampleScreen(TexCoord).rgb;
    int effectType = int(u_effectType + 0.5);

    vec3 effected;
    if (effectType == 1) {
        effected = applyCRT(TexCoord);
    } else if (effectType == 2) {
        effected = applyPosterize(original);
    } else if (effectType == 3) {
        effected = applyPixelize(TexCoord);
    } else if (effectType == 4) {
        effected = applySaturation(original);
    } else if (effectType == 5) {
        effected = applyVHS(TexCoord);
    } else if (effectType == 6) {
        effected = applyChromaticAberration(TexCoord);
    } else {
        FragColor = vec4(original, 1.0);
        return;
    }

    vec3 result = mix(original, effected, clamp(u_intensity, 0.0, 1.0));
    FragColor = vec4(result, 1.0);
}
@end

@program effect post_vs post_fs
