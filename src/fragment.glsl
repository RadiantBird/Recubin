#version 330 core
out vec4 FragColor;

in vec3 Normal;
in vec3 FragPos;
in vec2 TexCoord;
in vec4 FragPosLightSpace;

uniform sampler2D ourTexture;
uniform sampler2D shadowMap;
uniform float hasShadows;
uniform vec4 ourColor;
uniform vec3 lightDir;
uniform float brightness;
uniform vec3 viewPos;
uniform vec3 lightColor;
uniform float unlit;

float shadowCalc(vec4 fragPosLightSpace, vec3 norm, vec3 lightDirNorm) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }
    if (projCoords.z > 1.0) return 0.0;
    float currentDepth = projCoords.z;
    float bias = max(0.003 * (1.0 - dot(norm, lightDirNorm)), 0.0005);
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    return shadow / 9.0;
}

void main() {
    vec4 texColor = texture(ourTexture, TexCoord);
    vec3 baseColor = mix(ourColor.rgb, texColor.rgb * ourColor.rgb, texColor.a);

    if (unlit > 0.5) {
        FragColor = vec4(baseColor, ourColor.a);
        return;
    }

    float ambientStrength = 0.3;
    vec3 ambient = ambientStrength * lightColor;

    vec3 norm = normalize(Normal);
    vec3 lightDirNorm = normalize(-lightDir);
    float diff = max(dot(norm, lightDirNorm), 0.0);
    vec3 diffuse = diff * lightColor * brightness;

    float shadow = hasShadows * shadowCalc(FragPosLightSpace, norm, lightDirNorm);

    vec3 lighting = ambient + (1.0 - shadow) * diffuse;
    vec3 result = lighting * baseColor;

    FragColor = vec4(result, ourColor.a);
}
