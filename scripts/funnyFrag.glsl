#version 330 core

in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D screenTexture;
uniform vec2  u_resolution;
uniform float u_time;
uniform float u_intensity;
uniform float u_param1;
uniform float u_param2;

void main()
{
    vec2 uv = TexCoord;

    // -----------------------------
    // Parameters
    // -----------------------------
    // Param1: distortion strength
    // Param2: chromatic aberration strength
    //
    // 0 のままでも見えるように最低値を与える。
    float distortion = 0.015 + abs(u_param1) * 0.03;
    float chroma     = 0.001 + abs(u_param2) * 0.008;

    // -----------------------------
    // Aspect-corrected coordinates
    // -----------------------------
    vec2 centered = uv - 0.5;

    float aspect = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 p = centered;
    p.x *= aspect;

    float radius = length(p);

    // -----------------------------
    // Animated radial distortion
    // -----------------------------
    // 中央から同心円状に波が広がる。
    float wave =
        sin(radius * 35.0 - u_time * 4.0) *
        distortion;

    // 中央では弱く、少し外側で強くする。
    wave *= smoothstep(0.02, 0.65, radius);

    vec2 direction = vec2(0.0);

    if (radius > 0.0001)
    {
        direction = p / radius;
        direction.x /= aspect;
    }

    vec2 warpedUV = uv + direction * wave;

    // -----------------------------
    // Secondary liquid motion
    // -----------------------------
    // 放射波だけだと規則的すぎるので、
    // 小さい2軸の揺れを重ねる。
    warpedUV.x +=
        sin(
            uv.y * 24.0 +
            u_time * 1.7 +
            sin(uv.x * 9.0)
        ) * distortion * 0.25;

    warpedUV.y +=
        cos(
            uv.x * 19.0 -
            u_time * 1.3 +
            cos(uv.y * 11.0)
        ) * distortion * 0.20;

    // -----------------------------
    // Chromatic aberration
    // -----------------------------
    // 中心から離れるほどRGBを少し分離する。
    float chromaAmount =
        chroma *
        smoothstep(0.10, 0.80, radius);

    vec2 chromaOffset =
        direction * chromaAmount;

    float r = texture(
        screenTexture,
        warpedUV + chromaOffset
    ).r;

    float g = texture(
        screenTexture,
        warpedUV
    ).g;

    float b = texture(
        screenTexture,
        warpedUV - chromaOffset
    ).b;

    vec4 effectColor = vec4(r, g, b, 1.0);

    // -----------------------------
    // Subtle pulse
    // -----------------------------
    float pulse =
        1.0 +
        sin(u_time * 2.0 + radius * 12.0) * 0.035;

    effectColor.rgb *= pulse;

    // -----------------------------
    // Vignette
    // -----------------------------
    float vignette =
        1.0 -
        smoothstep(0.45, 0.95, radius) * 0.28;

    effectColor.rgb *= vignette;

    // -----------------------------
    // Original image
    // -----------------------------
    vec4 original =
        texture(screenTexture, TexCoord);

    // -----------------------------
    // Final mix
    // -----------------------------
    float intensity =
        clamp(u_intensity, 0.0, 1.0);

    FragColor =
        mix(original, effectColor, intensity);
}