#version 330 core
out vec4 FragColor;

in vec3 Normal;
in vec3 FragPos;
in vec2 TexCoord;
in vec4 FragPosLightSpace;
in vec3 VertexColor;
in float MatAlpha;
in vec3 LocalPos;
in vec3 LocalNormal;
in vec4 InstColor;

uniform sampler2D ourTexture;
uniform sampler2D shadowMap;
uniform float hasShadows;
uniform vec4 ourColor;
uniform vec3 lightDir;
uniform float brightness;
uniform vec3 viewPos;
uniform vec3 lightColor;
uniform float unlit;
uniform float u_textureScale;
uniform float useTriplanar;
uniform vec2 uvScale;
uniform float isSurfaceGui;
uniform float useVertexColor;
uniform float uInstanced;

// ---- 追加光源（Point/Spot）。方向光 lightDir は別扱い ----
#define MAX_LIGHTS 8
struct Light {
    int   type;        // 0=point, 1=spot
    vec3  position;
    vec3  direction;   // spot のコーン向き（正規化）
    vec3  color;
    float brightness;
    float range;
    float cosCutoff;   // spot: コーン外縁の cos（point では無視）
};
uniform Light uLights[MAX_LIGHTS];
uniform int   uLightCount;

// ---- MeshCube用UV空間Decal合成。uDecalCountはMeshCube描画時のみ>0で、他クラス描画時は常に0のためループがno-op ----
#define MAX_DECALS 8
uniform sampler2D uDecalTex[MAX_DECALS];
uniform vec2      uDecalCenter[MAX_DECALS];
uniform float     uDecalRadius[MAX_DECALS];
uniform vec4      uDecalColor[MAX_DECALS];
uniform int       uDecalCount;
uniform int       uDecalMode[MAX_DECALS];  // 0=UV円形, 1=Face面貼り
uniform int       uDecalFace[MAX_DECALS];  // Face番号(0=Front..5=Left)
uniform vec3      uLocalBoundsMin;         // MeshCubeローカルAABB
uniform vec3      uLocalBoundsMax;

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
    vec4 effColor = (uInstanced > 0.5) ? InstColor : ourColor;
    vec4 texColor;
    if (useTriplanar > 0.5) {
        float scale = u_textureScale > 0.0 ? u_textureScale : 1.0;
        vec2 uvX = FragPos.zy * scale;
        vec2 uvY = FragPos.xz * scale;
        vec2 uvZ = FragPos.xy * scale;
        vec4 colX = texture(ourTexture, uvX);
        vec4 colY = texture(ourTexture, uvY);
        vec4 colZ = texture(ourTexture, uvZ);
        vec3 blend = abs(normalize(Normal));
        blend /= (blend.x + blend.y + blend.z);
        texColor = colX * blend.x + colY * blend.y + colZ * blend.z;
    } else {
        vec2 scale = (uvScale.x > 0.0 && uvScale.y > 0.0) ? uvScale : vec2(1.0, 1.0);
        texColor = texture(ourTexture, TexCoord * scale);
    }
    vec3 baseColor;
    if (useVertexColor > 0.5) {
        baseColor = VertexColor;
    } else {
        baseColor = (isSurfaceGui > 0.5)
            ? mix(effColor.rgb, texColor.rgb, texColor.a)
            : mix(effColor.rgb, texColor.rgb * effColor.rgb, texColor.a);
    }

    // ---- UV空間Decal合成(MeshCube専用。他クラスはuDecalCount==0でno-op) ----
    for (int i = 0; i < uDecalCount; ++i) {
        if (uDecalMode[i] == 0) {
            vec2  d    = TexCoord - uDecalCenter[i];
            float dist = length(d);
            float edge = max(uDecalRadius[i], 1e-4);
            float mask = 1.0 - smoothstep(edge * 0.85, edge, dist);
            if (mask > 0.0) {
                vec2 localUV = d / edge * 0.5 + 0.5;
                vec4 dcol = texture(uDecalTex[i], localUV);
                float a = dcol.a * uDecalColor[i].a * mask;
                baseColor = mix(baseColor, dcol.rgb * uDecalColor[i].rgb, a);
            }
        } else {
            // Face面貼り: Cube.cpp createCubeVertices と同一のFace法線・UV軸規約
            vec3 faceN;
            if      (uDecalFace[i] == 0) faceN = vec3( 0.0,  0.0, -1.0); // Front
            else if (uDecalFace[i] == 1) faceN = vec3( 0.0,  0.0,  1.0); // Back
            else if (uDecalFace[i] == 2) faceN = vec3( 0.0,  1.0,  0.0); // Top
            else if (uDecalFace[i] == 3) faceN = vec3( 0.0, -1.0,  0.0); // Bottom
            else if (uDecalFace[i] == 4) faceN = vec3( 1.0,  0.0,  0.0); // Right
            else                         faceN = vec3(-1.0,  0.0,  0.0); // Left

            float ndot = dot(normalize(LocalNormal), faceN);
            float mask = smoothstep(0.35, 0.6, ndot);
            if (mask > 0.0) {
                vec3 extent = uLocalBoundsMax - uLocalBoundsMin;
                float tx = (LocalPos.x - uLocalBoundsMin.x) / max(extent.x, 1e-4);
                float ty = (LocalPos.y - uLocalBoundsMin.y) / max(extent.y, 1e-4);
                float tz = (LocalPos.z - uLocalBoundsMin.z) / max(extent.z, 1e-4);

                vec2 localUV;
                if      (uDecalFace[i] == 0) localUV = vec2(1.0 - tx, ty);       // Front
                else if (uDecalFace[i] == 1) localUV = vec2(tx, ty);             // Back
                else if (uDecalFace[i] == 2) localUV = vec2(1.0 - tx, tz);       // Top
                else if (uDecalFace[i] == 3) localUV = vec2(1.0 - tx, 1.0 - tz); // Bottom
                else if (uDecalFace[i] == 4) localUV = vec2(1.0 - tz, ty);       // Right
                else                         localUV = vec2(tz, ty);            // Left

                if (localUV.x >= 0.0 && localUV.x <= 1.0 && localUV.y >= 0.0 && localUV.y <= 1.0) {
                    vec4 dcol = texture(uDecalTex[i], localUV);
                    float a = dcol.a * uDecalColor[i].a * mask;
                    baseColor = mix(baseColor, dcol.rgb * uDecalColor[i].rgb, a);
                }
            }
        }
    }

    // texColor.a はどちらの分岐でも baseColor 側の mix() 済みなので、
    // 出力アルファに二重で掛けない（掛けるとテクスチャ/GUIの透明部分でキューブ自体が透けてしまう）
    float outAlpha = effColor.a * MatAlpha;

    if (unlit > 0.5) {
        FragColor = vec4(baseColor, outAlpha);
        return;
    }

    float ambientStrength = 0.3;
    vec3 ambient = ambientStrength * vec3(1.0);

    vec3 norm = normalize(Normal);
    vec3 lightDirNorm = normalize(-lightDir);
    float diff = max(dot(norm, lightDirNorm), 0.0);
    vec3 diffuse = diff * lightColor * brightness;

    float shadow = hasShadows * shadowCalc(FragPosLightSpace, norm, lightDirNorm);

    vec3 lighting = ambient + (1.0 - shadow) * diffuse;

    // ---- 追加 Point/Spot 光源を加算 ----
    for (int i = 0; i < uLightCount; ++i) {
        vec3  toL  = uLights[i].position - FragPos;
        float dist = length(toL);
        vec3  Ldir = toL / max(dist, 1e-4);
        float atten = clamp(1.0 - dist / max(uLights[i].range, 1e-4), 0.0, 1.0);
        atten *= atten;
        float d = max(dot(norm, Ldir), 0.0);
        float cone = 1.0;
        if (uLights[i].type == 1) {
            float theta = dot(normalize(-Ldir), uLights[i].direction);
            cone = step(uLights[i].cosCutoff, theta);  // 簡易ハードエッジ
        }
        lighting += d * atten * cone * uLights[i].color * uLights[i].brightness;
    }

    vec3 result = lighting * baseColor;

    FragColor = vec4(result, outAlpha);
}
