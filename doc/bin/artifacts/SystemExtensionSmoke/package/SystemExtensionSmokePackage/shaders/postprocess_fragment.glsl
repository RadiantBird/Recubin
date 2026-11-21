#version 330 core
out vec4 FragColor;
in vec2 TexCoord;

uniform sampler2D screenTexture;
uniform vec2  u_resolution;
uniform int   u_effectType; // 0=None 1=CRT 2=Posterization 3=Pixelize 4=Saturation 5=VHS 6=ChromaticAberration
uniform float u_intensity;  // 元画像とのブレンド比率 (0..1)
uniform float u_param1;     // ScanlineCount(CRT) / Levels(Posterization) / PixelSize(Pixelize) / SaturationAmount(Saturation) / NoiseAmount(VHS) / Offset(ChromaticAberration)
uniform float u_param2;     // CurveAmount(CRT)。他タイプでは未使用
uniform float u_time;       // 経過時間（秒）。VHSのノイズに使用

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

    vec3 col = texture(screenTexture, distortedUv).rgb;

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
    return texture(screenTexture, snapped).rgb;
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

    vec3 col = texture(screenTexture, jitteredUv).rgb;

    // 粒状ノイズ
    float grain = hash(uv * u_resolution + u_time) - 0.5;
    col += grain * noiseAmount * 0.25;

    return col;
}

vec3 applyChromaticAberration(vec2 uv) {
    vec2 dir = uv - 0.5;
    vec2 offset = dir * u_param1 * 0.02;

    float r = texture(screenTexture, uv + offset).r;
    float g = texture(screenTexture, uv).g;
    float b = texture(screenTexture, uv - offset).b;

    return vec3(r, g, b);
}

void main() {
    vec3 original = texture(screenTexture, TexCoord).rgb;

    vec3 effected;
    if (u_effectType == 1) {
        effected = applyCRT(TexCoord);
    } else if (u_effectType == 2) {
        effected = applyPosterize(original);
    } else if (u_effectType == 3) {
        effected = applyPixelize(TexCoord);
    } else if (u_effectType == 4) {
        effected = applySaturation(original);
    } else if (u_effectType == 5) {
        effected = applyVHS(TexCoord);
    } else if (u_effectType == 6) {
        effected = applyChromaticAberration(TexCoord);
    } else {
        FragColor = vec4(original, 1.0);
        return;
    }

    vec3 result = mix(original, effected, clamp(u_intensity, 0.0, 1.0));
    FragColor = vec4(result, 1.0);
}
