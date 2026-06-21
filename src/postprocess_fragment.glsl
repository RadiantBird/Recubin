#version 330 core
out vec4 FragColor;
in vec2 TexCoord;

uniform sampler2D screenTexture;
uniform vec2  u_resolution;
uniform int   u_effectType; // 0=None 1=CRT 2=Posterization 3=Pixelize
uniform float u_intensity;  // 元画像とのブレンド比率 (0..1)
uniform float u_param1;     // ScanlineCount(CRT) / Levels(Posterization) / PixelSize(Pixelize)
uniform float u_param2;     // CurveAmount(CRT)。他タイプでは未使用

vec3 applyCRT(vec2 uv) {
    // バレル歪み（簡易）
    vec2 centered = uv * 2.0 - 1.0;
    float dist = dot(centered, centered);
    vec2 distortedUv = (centered * (1.0 + u_param2 * dist)) * 0.5 + 0.5;

    vec3 col = texture(screenTexture, distortedUv).rgb;

    float scanlines = max(u_param1, 1.0);
    float scan = sin(distortedUv.y * scanlines * 3.14159265);
    col *= 1.0 - 0.25 * (0.5 - 0.5 * scan);

    return col;
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

void main() {
    vec3 original = texture(screenTexture, TexCoord).rgb;

    vec3 effected;
    if (u_effectType == 1) {
        effected = applyCRT(TexCoord);
    } else if (u_effectType == 2) {
        effected = applyPosterize(original);
    } else if (u_effectType == 3) {
        effected = applyPixelize(TexCoord);
    } else {
        FragColor = vec4(original, 1.0);
        return;
    }

    vec3 result = mix(original, effected, clamp(u_intensity, 0.0, 1.0));
    FragColor = vec4(result, 1.0);
}
