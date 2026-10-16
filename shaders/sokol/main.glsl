// メインシェーダー（BaseCube・地形・天体・Skybox の焼き込みなど。Renderer::renderViewport ほか）
// 元は shaders/vertex.glsl / fragment.glsl。ユニフォームは MainShaderState が保持し、描画の直前に flush で送る。
@module main

@vs main_vs
// フレームごとの値（Renderer::renderViewport の冒頭など）
layout(binding = 0) uniform vs_frame {
    mat4  view;
    mat4  projection;
    float uTime;      // 経過秒（波アニメ用）
};
// 描画ごとの値
layout(binding = 1) uniform vs_object {
    mat4  model;
    vec4  uPbrMaterial; // 個別描画用のPBR値 (metallic, roughness, reflectance, enabled)。w=0でPBR無効
    float uIsLiquid;    // LiquidCube 描画時 1.0
    float uInstanced;   // 1.0 でインスタンス属性からモデル行列と色を取る
};

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec3 aVertexColor; // Terrain 頂点カラー
layout(location = 4) in float aMatAlpha;   // MeshCube マテリアルアルファ
// インスタンス描画時のみ有効(divisor=1)。モデル行列の列（Renderer::setInstanceAttribPointers の属性 5-8）
layout(location = 5) in vec4 aInstModel0;
layout(location = 6) in vec4 aInstModel1;
layout(location = 7) in vec4 aInstModel2;
layout(location = 8) in vec4 aInstModel3;
layout(location = 9) in vec4 aInstColor;   // インスタンスごとの色
layout(location = 10) in vec4 aInstPbr;    // インスタンスごとのPBR値 (metallic, roughness, reflectance, enabled)

layout(location = 0) flat out vec4 PbrParams;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec3 FragPos;
layout(location = 3) out vec2 TexCoord;
layout(location = 4) out float ViewDepth;
layout(location = 5) out vec3 VertexColor;
layout(location = 6) out float MatAlpha;
layout(location = 7) out vec3 LocalPos;
layout(location = 8) out vec3 LocalNormal;
layout(location = 9) out vec4 InstColor;

void main() {
    mat4 m = (uInstanced > 0.5) ? mat4(aInstModel0, aInstModel1, aInstModel2, aInstModel3) : model;
    // LiquidCube は上面のみ時間ベースの sin 波で揺らす（ローカル空間）
    vec3 p = aPos;
    if (uIsLiquid > 0.5 && aPos.y > 0.0) {
        const float WAVE_ANGULAR_SPEED = 1.5;
        const float WAVE_SPATIAL_FREQUENCY = 4.0;
        const float WAVE_AMPLITUDE = 0.06;
        p.y += sin(uTime * WAVE_ANGULAR_SPEED +
                   aPos.x * WAVE_SPATIAL_FREQUENCY +
                   aPos.z * WAVE_SPATIAL_FREQUENCY) * WAVE_AMPLITUDE;
    }
    FragPos = vec3(m * vec4(p, 1.0));
    Normal = mat3(transpose(inverse(m))) * aNormal;
    TexCoord = aTexCoord;
    VertexColor = aVertexColor;
    MatAlpha = aMatAlpha;
    LocalPos = p;
    LocalNormal = aNormal;
    InstColor = (uInstanced > 0.5) ? aInstColor : vec4(1.0);
    PbrParams = (uInstanced > 0.5) ? aInstPbr : uPbrMaterial;
    vec4 viewPosition = view * vec4(FragPos, 1.0);
    ViewDepth = -viewPosition.z;
    gl_Position = projection * viewPosition;
}
@end

@fs main_fs
#define MAX_LIGHTS 8
#define MAX_DECALS 8

// フレームごとの値
layout(binding = 2) uniform fs_frame {
    mat4  lightSpaceMatrices[3];
    vec3  uShadowCascadeSplits;
    float hasShadows;
    vec3  lightDir;
    float brightness;
    vec3  lightColor;
    float uEnvMaxLod;   // 環境キューブマップの最大mipレベル(roughness=1に対応)
    vec3  viewPos;
    float uLightCount;  // 追加光源の数（整数値）
    vec2  uShadowCascadeBlend;
    float uShadowDistance;
    float uShadowFadeDistance;
    // ---- 追加光源（Point/Spot）。方向光 lightDir は別扱い ----
    vec4  uLightPosRange[MAX_LIGHTS];    // xyz=位置, w=range
    vec4  uLightDirCutoff[MAX_LIGHTS];   // xyz=spot のコーン向き（正規化）, w=spot のコーン外縁の cos（point では無視）
    vec4  uLightColorBright[MAX_LIGHTS]; // rgb=色, a=明るさ
    vec4  uLightType[MAX_LIGHTS];        // x: 0=point, 1=spot
};

// 描画ごとの値
layout(binding = 3) uniform fs_object {
    vec4  ourColor;
    vec4  uTextureTintColor;
    vec4  uMatMapFlags;      // x=BaseColor y=Roughness z=Metallic w=Normal (1=有効)
    vec3  uLocalBoundsMin;   // MeshCubeローカルAABB
    float unlit;
    vec3  uLocalBoundsMax;
    float u_textureScale;
    vec2  uvScale;
    vec2  uMatMapParams;     // x=1タイルあたりのstud数, y=法線マップの強さ
    float useTriplanar;
    float isSurfaceGui;
    float useVertexColor;
    float uInstanced;
    float uUseTextureTint;
};

// SurfaceMark is rendered by re-drawing only BaseCube candidates.  The mark
// transform maps world positions into the mark's local space (origin is the
// near plane, and -Z is the projection direction).  Its depth map contains
// the first surface visible from the mark for each projected pixel.
layout(binding = 4) uniform fs_pass {
    mat4  uSurfaceMarkWorldToLocal;
    mat4  uSurfaceMarkProjection;
    vec4  uSurfaceMarkTint;
    vec3  uSurfaceMarkSize;
    float uSurfaceMarkPass;
};

// ---- MeshCube用UV空間Decal合成。uDecalCountはMeshCube描画時のみ>0で、他クラス描画時は常に0のためループがno-op ----
layout(binding = 5) uniform fs_decals {
    vec4  uDecalCenterRadius[MAX_DECALS]; // xy=中心UV, z=半径, w=配列テクスチャのレイヤー
    vec4  uDecalColor[MAX_DECALS];
    vec4  uDecalModeFace[MAX_DECALS];     // x: 0=UV円形, 1=Face面貼り / y: Face番号(0=Front..5=Left)
    float uDecalCount;                    // 整数値
};

// テクスチャ。ユニット番号は texture-sampler の組の順番（= binding の順）で、Renderer の
// SHADOW_MAP_UNIT / SURFACE_MARK_*_UNIT / DECAL_ARRAY_UNIT / ENV_SPECULAR_UNIT / MATERIAL_MAP_UNIT_BASE と一致させること。
layout(binding = 0) uniform texture2D ourTexture;
layout(binding = 0) uniform sampler ourTextureSampler;
layout(binding = 1) uniform texture2DArray shadowMap;
layout(binding = 1) uniform sampler shadowMapSampler;
layout(binding = 2) uniform texture2D uSurfaceMarkTexture;
layout(binding = 2) uniform sampler uSurfaceMarkTextureSampler;
layout(binding = 3) uniform texture2D uSurfaceMarkDepth;
layout(binding = 3) uniform sampler uSurfaceMarkDepthSampler;
layout(binding = 4) uniform texture2DArray uDecalTexArray;
layout(binding = 4) uniform sampler uDecalTexArraySampler;
// ---- PBR用の環境反射。Skyboxの6面から焼いた、roughnessごとにぼかしたキューブマップ ----
layout(binding = 5) uniform textureCube uEnvSpecular;
layout(binding = 5) uniform sampler uEnvSpecularSampler;
// Materialの画像マップ(個別描画のときだけ有効)。ワールド基準のtriplanarで貼る。
layout(binding = 6) uniform texture2D uMatBaseColorMap;
layout(binding = 6) uniform sampler uMatBaseColorMapSampler;
layout(binding = 7) uniform texture2D uMatRoughnessMap;
layout(binding = 7) uniform sampler uMatRoughnessMapSampler;
layout(binding = 8) uniform texture2D uMatMetallicMap;
layout(binding = 8) uniform sampler uMatMetallicMapSampler;
layout(binding = 9) uniform texture2D uMatNormalMap;
layout(binding = 9) uniform sampler uMatNormalMapSampler;

out vec4 FragColor;

layout(location = 1) in vec3 Normal;
layout(location = 2) in vec3 FragPos;
layout(location = 3) in vec2 TexCoord;
layout(location = 4) in float ViewDepth;
layout(location = 5) in vec3 VertexColor;
layout(location = 6) in float MatAlpha;
layout(location = 7) in vec3 LocalPos;
layout(location = 8) in vec3 LocalNormal;
layout(location = 9) in vec4 InstColor;
layout(location = 0) flat in vec4 PbrParams;  // (metallic, roughness, reflectance, enabled)。w<=0.5なら従来のLambert描画

float shadowCalc(vec4 fragPosLightSpace, vec3 norm, vec3 lightDirNorm, int cascadeIndex) {
    // The light-space projection is a homogeneous clip-space position.  Do
    // not attempt a divide for vertices behind the light camera: an invalid
    // w can turn into an apparently valid UV and make the shadow test depend
    // on the sampler border state.
    if (fragPosLightSpace.w <= 0.0) return 0.0;
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0) {
        return 0.0;
    }
    vec3 shadowNormal = normalize(norm);
    vec3 shadowLightDir = normalize(lightDirNorm);
    float currentDepth = projCoords.z;
    float normalLight = clamp(dot(shadowNormal, shadowLightDir), 0.0, 1.0);
    // The bias is in the projected depth domain. Keep the minimum small so
    // contact shadows remain attached, and increase it only as the receiver
    // becomes grazing to the light direction.
    const float MIN_SHADOW_BIAS = 0.00035;
    const float SLOPE_SHADOW_BIAS = 0.0012;
    float bias = max(SLOPE_SHADOW_BIAS * (1.0 - normalLight), MIN_SHADOW_BIAS);
    float shadow = 0.0;
    float sampleCount = 0.0;
    ivec2 mapSize = textureSize(sampler2DArray(shadowMap, shadowMapSampler), 0).xy;
    vec2 texelSize = 1.0 / vec2(mapSize);
    ivec2 centerTexel = ivec2(floor(projCoords.xy / texelSize));
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            // Keep the PCF kernel from consulting the border value.  The
            // explicit range test above handles the fragment itself, while
            // this test handles the three-by-three footprint at the edge.
            // Out-of-coverage samples are lit, never shadowed.
            ivec2 sampleTexel = centerTexel + ivec2(x, y);
            if (sampleTexel.x < 0 || sampleTexel.x >= mapSize.x ||
                sampleTexel.y < 0 || sampleTexel.y >= mapSize.y) {
                continue;
            }
            float pcfDepth = texelFetch(sampler2DArray(shadowMap, shadowMapSampler), ivec3(sampleTexel, cascadeIndex), 0).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
            sampleCount += 1.0;
        }
    return sampleCount > 0.0 ? shadow / sampleCount : 0.0;
}

float shadowForCascade(int cascadeIndex, vec3 norm, vec3 lightDirNorm) {
    vec4 fragPosLightSpace = lightSpaceMatrices[cascadeIndex] * vec4(FragPos, 1.0);
    return shadowCalc(fragPosLightSpace, norm, lightDirNorm, cascadeIndex);
}

// Point/Spot光源iの減衰（円錐込み）と、フラグメントから光源へ向かう方向を返す。
// 既存のLambert描画とPBR描画で共用する。
float pointSpotAttenuation(int i, out vec3 Ldir) {
    vec3  toL  = uLightPosRange[i].xyz - FragPos;
    float dist = length(toL);
    Ldir = toL / max(dist, 1e-4);
    float atten = clamp(1.0 - dist / max(uLightPosRange[i].w, 1e-4), 0.0, 1.0);
    atten *= atten;
    float cone = 1.0;
    if (uLightType[i].x > 0.5) {
        float theta = dot(normalize(-Ldir), uLightDirCutoff[i].xyz);
        cone = step(uLightDirCutoff[i].w, theta);  // 簡易ハードエッジ
    }
    return atten * cone;
}

// ---- PBR (metallic-roughness) ----
// パイプライン全体が非リニア(sRGBデコードなし)のため、PBR個体も同じ表示空間で計算する。
// 既存Lambertの拡散(1/πなし)と明るさを揃えるため、スペキュラ側にπを掛けて合成する。
const float PBR_PI = 3.14159265;

float pbrDistributionGGX(float NdotH, float alpha) {
    float a2 = alpha * alpha;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PBR_PI * d * d, 1e-6);
}

float pbrGeometrySmith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

vec3 pbrFresnel(float cosTheta, vec3 F0) {
    return F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Karis(UE4 mobile)の解析的EnvBRDF近似。BRDF LUTを焼かずに済ませる。
vec3 pbrEnvBrdfApprox(vec3 F0, float roughness, float NdotV) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
    return F0 * AB.x + vec3(AB.y);
}

// 1つの光源からの寄与。lightIrradianceは従来Lambertと同じ単位（色*明るさ*減衰、N·Lは含まない）。
vec3 pbrDirectLight(vec3 N, vec3 V, vec3 L, vec3 lightIrradiance,
                    vec3 albedo, float metallic, float roughness, vec3 F0) {
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);
    vec3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    float D = pbrDistributionGGX(NdotH, roughness * roughness);
    float G = pbrGeometrySmith(NdotV, NdotL, roughness);
    vec3  F = pbrFresnel(VdotH, F0);
    vec3 specular = (D * G) * F / max(4.0 * NdotV * NdotL, 1e-4);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    return (kD * albedo + specular * PBR_PI) * lightIrradiance * NdotL;
}

vec3 matTriplanarWeights(vec3 n) {
    vec3 w = pow(abs(n), vec3(4.0));
    return w / max(w.x + w.y + w.z, 1e-4);
}

vec4 matSampleTriplanar(texture2D tex, sampler smp, vec3 p, vec3 w) {
    vec3 q = p / max(uMatMapParams.x, 1e-3);
    return texture(sampler2D(tex, smp), q.zy) * w.x + texture(sampler2D(tex, smp), q.xz) * w.y +
           texture(sampler2D(tex, smp), q.xy) * w.z;
}

// タンジェント空間の法線マップをtriplanarで貼る(whiteout blend)。
vec3 matPerturbNormal(vec3 N, vec3 p, vec3 w) {
    vec3 q = p / max(uMatMapParams.x, 1e-3);
    vec3 tx = texture(sampler2D(uMatNormalMap, uMatNormalMapSampler), q.zy).xyz * 2.0 - 1.0;
    vec3 ty = texture(sampler2D(uMatNormalMap, uMatNormalMapSampler), q.xz).xyz * 2.0 - 1.0;
    vec3 tz = texture(sampler2D(uMatNormalMap, uMatNormalMapSampler), q.xy).xyz * 2.0 - 1.0;
    tx.xy *= uMatMapParams.y;
    ty.xy *= uMatMapParams.y;
    tz.xy *= uMatMapParams.y;
    tx = vec3(tx.xy + N.zy, abs(tx.z) * N.x);
    ty = vec3(ty.xy + N.xz, abs(ty.z) * N.y);
    tz = vec3(tz.xy + N.xy, abs(tz.z) * N.z);
    return normalize(tx.zyx * w.x + ty.xzy * w.y + tz.xyz * w.z);
}

vec3 pbrShade(vec3 albedo, vec3 N, float shadow, float metallic, float roughness) {
    float reflectance = clamp(PbrParams.z, 0.0, 1.0);
    vec3  V  = normalize(viewPos - FragPos);
    vec3  F0 = mix(vec3(0.16 * reflectance * reflectance), albedo, metallic);

    vec3 Lo = pbrDirectLight(N, V, normalize(-lightDir), lightColor * brightness * (1.0 - shadow),
                             albedo, metallic, roughness, F0);
    for (int i = 0; i < int(uLightCount + 0.5); ++i) {
        vec3 Ldir;
        float atten = pointSpotAttenuation(i, Ldir);
        Lo += pbrDirectLight(N, V, Ldir, uLightColorBright[i].rgb * uLightColorBright[i].a * atten,
                             albedo, metallic, roughness, F0);
    }

    // 拡散アンビエントは従来(0.3)と揃え、金属は拡散を持たない。環境はスペキュラ反射のみに使う。
    vec3 ambientDiffuse = (1.0 - metallic) * albedo * 0.3;
    vec3 R = reflect(-V, N);
    vec3 prefiltered = textureLod(samplerCube(uEnvSpecular, uEnvSpecularSampler), R, roughness * uEnvMaxLod).rgb;
    vec3 ambientSpecular = prefiltered * pbrEnvBrdfApprox(F0, roughness, max(dot(N, V), 1e-4));
    return ambientDiffuse + ambientSpecular + Lo;
}

void main() {
    vec4 effColor = (uInstanced > 0.5) ? InstColor : ourColor;
    vec4 texColor;
if (useTriplanar > 0.5) {
        float scale = u_textureScale > 0.0 ? u_textureScale : 1.0;
        
        // 1. まず、LocalPosから計算したUV（これはそのまま）
        vec2 uvX = LocalPos.zy * scale;
        vec2 uvY = LocalPos.xz * scale;
        vec2 uvZ = LocalPos.xy * scale;

        // 2. 【追加】カメラ距離に依存しない標準的なUV勾配（TexCoord由来）を計算する
        vec2 d_dx = dFdx(TexCoord * scale);
        vec2 d_dy = dFdy(TexCoord * scale);

        // 3. 【修正】texture() の代わりに textureGrad() を使い、勾配(d_dx, d_dy)を強制する
        vec4 colX = textureGrad(sampler2D(ourTexture, ourTextureSampler), uvX, d_dx, d_dy);
        vec4 colY = textureGrad(sampler2D(ourTexture, ourTextureSampler), uvY, d_dx, d_dy);
        vec4 colZ = textureGrad(sampler2D(ourTexture, ourTextureSampler), uvZ, d_dx, d_dy);
        // LocalNormal を使用してブレンドウェイトを計算
        vec3 blend = abs(LocalNormal);
        // blend の境界をなめらかにして、スキャンラインアーティファクトを軽減
        // smoothstep で遷移をなめらかにし、合計が常に 1.0 になるよう正規化
        vec3 blendSmooth = smoothstep(0.0, 0.3, blend);
        float blendSum = max(blendSmooth.x + blendSmooth.y + blendSmooth.z, 0.001);
        blendSmooth /= blendSum;
        texColor = colX * blendSmooth.x + colY * blendSmooth.y + colZ * blendSmooth.z;
    } else {
        vec2 scale = (uvScale.x > 0.0 && uvScale.y > 0.0) ? uvScale : vec2(1.0, 1.0);
        texColor = texture(sampler2D(ourTexture, ourTextureSampler), TexCoord * scale);
    }
    vec3 baseColor;
    if (useVertexColor > 0.5) {
        baseColor = VertexColor;
    } else {
        vec3 finalTexColor = texColor.rgb;
        float textureBlendAlpha = texColor.a;
        if (uUseTextureTint > 0.5) {
            finalTexColor *= uTextureTintColor.rgb;
            textureBlendAlpha *= uTextureTintColor.a;
        }
        baseColor = (isSurfaceGui > 0.5)
            ? mix(effColor.rgb, texColor.rgb, texColor.a)
            : mix(effColor.rgb, finalTexColor, textureBlendAlpha);
    }

    // SurfaceMark overlay. During this dedicated candidate redraw the renderer
    // sets uDecalCount=0, so this early branch is not contaminated by Decal
    // composition; the ordinary pass (uSurfaceMarkPass=0) is unchanged.
    if (uSurfaceMarkPass > 0.5) {
        vec3 markLocal = (uSurfaceMarkWorldToLocal * vec4(FragPos, 1.0)).xyz;
        vec3 markExtent = max(uSurfaceMarkSize, vec3(1e-4));
        // The mark plane is z=0 and projects towards -Z.
        if (markLocal.x < -markExtent.x * 0.5 || markLocal.x > markExtent.x * 0.5 ||
            markLocal.y < -markExtent.y * 0.5 || markLocal.y > markExtent.y * 0.5 ||
            markLocal.z > 0.0 || markLocal.z < -markExtent.z) {
            discard;
        }
        vec4 markClip = uSurfaceMarkProjection * vec4(FragPos, 1.0);
        vec3 markNdc = markClip.xyz / max(markClip.w, 1e-6);
        vec2 markUv = markNdc.xy * 0.5 + 0.5;
        if (markUv.x < 0.0 || markUv.x > 1.0 || markUv.y < 0.0 || markUv.y > 1.0) discard;
        float nearestDepth = texture(sampler2D(uSurfaceMarkDepth, uSurfaceMarkDepthSampler), markUv).r;
        float currentDepth = markNdc.z * 0.5 + 0.5;
        // A small epsilon prevents coplanar precision noise while retaining
        // only the first surface recorded in the mark depth pass.
        if (currentDepth > nearestDepth + 0.0015) discard;
        vec4 markColor = texture(sampler2D(uSurfaceMarkTexture, uSurfaceMarkTextureSampler), vec2(markUv.x, markUv.y));
        float markAlpha = markColor.a * uSurfaceMarkTint.a * effColor.a;
        if (markAlpha <= 0.001) discard;
        baseColor = markColor.rgb * uSurfaceMarkTint.rgb;
        // Overlay alpha is finalized below together with the normal material
        // path, preserving the existing lighting and blend state.
        effColor.a = markAlpha;
    }

    // ---- UV空間Decal合成(MeshCube専用。他クラスはuDecalCount==0でno-op) ----
    for (int i = 0; i < int(uDecalCount + 0.5); ++i) {
        int decalFace = int(uDecalModeFace[i].y + 0.5);
        if (int(uDecalModeFace[i].x + 0.5) == 0) {
            vec2  d    = TexCoord - uDecalCenterRadius[i].xy;
            float dist = length(d);
            float edge = max(uDecalCenterRadius[i].z, 1e-4);
            float mask = 1.0 - smoothstep(edge * 0.85, edge, dist);
            if (mask > 0.0) {
                vec2 localUV = d / edge * 0.5 + 0.5;
                vec4 dcol = texture(sampler2DArray(uDecalTexArray, uDecalTexArraySampler), vec3(localUV, uDecalCenterRadius[i].w));
                float a = dcol.a * uDecalColor[i].a * mask;
                baseColor = mix(baseColor, dcol.rgb * uDecalColor[i].rgb, a);
            }
        } else {
            // Face面貼り: Cube.cpp createCubeVertices と同一のFace法線・UV軸規約
            vec3 faceN;
            if      (decalFace == 0) faceN = vec3( 0.0,  0.0, -1.0); // Front
            else if (decalFace == 1) faceN = vec3( 0.0,  0.0,  1.0); // Back
            else if (decalFace == 2) faceN = vec3( 0.0,  1.0,  0.0); // Top
            else if (decalFace == 3) faceN = vec3( 0.0, -1.0,  0.0); // Bottom
            else if (decalFace == 4) faceN = vec3( 1.0,  0.0,  0.0); // Right
            else                         faceN = vec3(-1.0,  0.0,  0.0); // Left

            float ndot = dot(normalize(LocalNormal), faceN);
            float mask = smoothstep(0.35, 0.6, ndot);
            if (mask > 0.0) {
                vec3 extent = uLocalBoundsMax - uLocalBoundsMin;
                float tx = (LocalPos.x - uLocalBoundsMin.x) / max(extent.x, 1e-4);
                float ty = (LocalPos.y - uLocalBoundsMin.y) / max(extent.y, 1e-4);
                float tz = (LocalPos.z - uLocalBoundsMin.z) / max(extent.z, 1e-4);

                vec2 localUV;
                if      (decalFace == 0) localUV = vec2(1.0 - tx, ty);       // Front
                else if (decalFace == 1) localUV = vec2(tx, ty);             // Back
                else if (decalFace == 2) localUV = vec2(1.0 - tx, tz);       // Top
                else if (decalFace == 3) localUV = vec2(1.0 - tx, 1.0 - tz); // Bottom
                else if (decalFace == 4) localUV = vec2(1.0 - tz, ty);       // Right
                else                         localUV = vec2(tz, ty);            // Left

                if (localUV.x >= 0.0 && localUV.x <= 1.0 && localUV.y >= 0.0 && localUV.y <= 1.0) {
                    vec4 dcol = texture(sampler2DArray(uDecalTexArray, uDecalTexArraySampler), vec3(localUV, uDecalCenterRadius[i].w));
                    float a = dcol.a * uDecalColor[i].a * mask;
                    baseColor = mix(baseColor, dcol.rgb * uDecalColor[i].rgb, a);
                }
            }
        }
    }

    // Image alpha has already been used to composite its RGB over the Cube
    // color.  It must not make the underlying Cube transparent.
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

    float shadowFade = 0.0;
    float fadeDistance = max(uShadowDistance, 0.0);
    float fragmentDistance = length(FragPos - viewPos);
    if (uShadowFadeDistance <= 0.0) {
        shadowFade = step(fragmentDistance, fadeDistance);
    } else if (fragmentDistance < fadeDistance) {
        float fadeStart = max(fadeDistance - uShadowFadeDistance, 0.0);
        shadowFade = 1.0 - smoothstep(fadeStart, fadeDistance, fragmentDistance);
    }

    // Select the cascade using the same view-space depth produced by the
    // vertex shader. Only the two cascades around a split are sampled, and
    // only within a small transition band, so ordinary fragments pay for one
    // PCF lookup while split boundaries remain free of a hard line.
    float cascadedShadow = 0.0;
    if (hasShadows > 0.5 && shadowFade > 0.0 && ViewDepth >= 0.0 &&
        ViewDepth <= uShadowCascadeSplits.z) {
        int cascadeIndex = ViewDepth < uShadowCascadeSplits.x ? 0 :
                           (ViewDepth < uShadowCascadeSplits.y ? 1 : 2);
        if (cascadeIndex == 0) {
            cascadedShadow = shadowForCascade(0, norm, lightDirNorm);
            if (ViewDepth > uShadowCascadeSplits.x - uShadowCascadeBlend.x) {
                float blend = smoothstep(
                    uShadowCascadeSplits.x - uShadowCascadeBlend.x,
                    uShadowCascadeSplits.x + uShadowCascadeBlend.x,
                    ViewDepth);
                cascadedShadow = mix(cascadedShadow,
                                     shadowForCascade(1, norm, lightDirNorm), blend);
            }
        } else if (cascadeIndex == 1) {
            cascadedShadow = shadowForCascade(1, norm, lightDirNorm);
            if (ViewDepth < uShadowCascadeSplits.x + uShadowCascadeBlend.x) {
                float blend = smoothstep(
                    uShadowCascadeSplits.x - uShadowCascadeBlend.x,
                    uShadowCascadeSplits.x + uShadowCascadeBlend.x,
                    ViewDepth);
                cascadedShadow = mix(shadowForCascade(0, norm, lightDirNorm),
                                     cascadedShadow, blend);
            } else if (ViewDepth > uShadowCascadeSplits.y - uShadowCascadeBlend.y) {
                float blend = smoothstep(
                    uShadowCascadeSplits.y - uShadowCascadeBlend.y,
                    uShadowCascadeSplits.y + uShadowCascadeBlend.y,
                    ViewDepth);
                cascadedShadow = mix(cascadedShadow,
                                     shadowForCascade(2, norm, lightDirNorm), blend);
            }
        } else {
            cascadedShadow = shadowForCascade(2, norm, lightDirNorm);
            if (ViewDepth < uShadowCascadeSplits.y + uShadowCascadeBlend.y) {
                float blend = smoothstep(
                    uShadowCascadeSplits.y - uShadowCascadeBlend.y,
                    uShadowCascadeSplits.y + uShadowCascadeBlend.y,
                    ViewDepth);
                cascadedShadow = mix(shadowForCascade(1, norm, lightDirNorm),
                                     cascadedShadow, blend);
            }
        }
    }
    float shadow = hasShadows * cascadedShadow * shadowFade;

    // Materialを参照するBaseCubeはPBRで描画する。それ以外は従来のLambert。
    if (PbrParams.w > 0.5) {
        float metallic  = clamp(PbrParams.x, 0.0, 1.0);
        float roughness = PbrParams.y;
        vec3  pbrAlbedo = baseColor;
        vec3  pbrNormal = norm;
        // 画像マップは uMatMapFlags が有効なときだけ使う(Materialごとの描画で束縛される。
        // インスタンス描画でもMaterialごとの区間で束縛し、それ以外では0にしてある)。
        if (uMatMapFlags.x + uMatMapFlags.y + uMatMapFlags.z + uMatMapFlags.w > 0.5) {
            vec3 mapWeights = matTriplanarWeights(norm);
            if (uMatMapFlags.x > 0.5) pbrAlbedo *= matSampleTriplanar(uMatBaseColorMap, uMatBaseColorMapSampler, FragPos, mapWeights).rgb;
            if (uMatMapFlags.y > 0.5) roughness *= matSampleTriplanar(uMatRoughnessMap, uMatRoughnessMapSampler, FragPos, mapWeights).r;
            if (uMatMapFlags.z > 0.5) metallic  *= matSampleTriplanar(uMatMetallicMap, uMatMetallicMapSampler, FragPos, mapWeights).r;
            if (uMatMapFlags.w > 0.5) pbrNormal = matPerturbNormal(norm, FragPos, mapWeights);
        }
        metallic  = clamp(metallic, 0.0, 1.0);
        roughness = clamp(roughness, 0.045, 1.0);  // 0付近は鏡面ハイライトが点に潰れるため下限を設ける
        FragColor = vec4(pbrShade(pbrAlbedo, pbrNormal, shadow, metallic, roughness), outAlpha);
        return;
    }

    vec3 lighting = ambient + (1.0 - shadow) * diffuse;

    // ---- 追加 Point/Spot 光源を加算 ----
    for (int i = 0; i < int(uLightCount + 0.5); ++i) {
        vec3  Ldir;
        float atten = pointSpotAttenuation(i, Ldir);
        float d = max(dot(norm, Ldir), 0.0);
        lighting += d * atten * uLightColorBright[i].rgb * uLightColorBright[i].a;
    }

    vec3 result = lighting * baseColor;

    FragColor = vec4(result, outAlpha);
}
@end

@program draw main_vs main_fs
