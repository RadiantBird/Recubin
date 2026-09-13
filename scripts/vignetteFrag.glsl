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
    vec4 original = texture(screenTexture, TexCoord);

    // 画面中央を原点にする。
    vec2 p = TexCoord - 0.5;

    // 画面比率を補正して、横長画面でも自然な円形になるようにする。
    float aspect = u_resolution.x / max(u_resolution.y, 1.0);
    p.x *= aspect;

    float distanceFromCenter = length(p);

    // Param1:
    //   ビグネットが始まる位置。
    //   0 の場合は扱いやすいデフォルト値を使用。
    //
    // Param2:
    //   ビグネット境界の柔らかさ。
    float radius =
        (u_param1 > 0.0)
        ? u_param1
        : 0.45;

    float softness =
        (u_param2 > 0.0)
        ? u_param2
        : 0.35;

    float vignette =
        1.0 - smoothstep(
            radius,
            radius + softness,
            distanceFromCenter
        );

    // u_intensity = 0 なら元画像そのまま。
    // u_intensity = 1 ならビグネット最大。
    float strength =
        clamp(u_intensity, 0.0, 1.0);

    float brightness =
        mix(
            1.0,
            vignette,
            strength
        );

    FragColor = vec4(
        original.rgb * brightness,
        original.a
    );
}