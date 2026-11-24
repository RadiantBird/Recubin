struct VSInput {
    float3 position : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 color : TEXCOORD2;
    float3 normal : TEXCOORD3;
};

struct VSOutput {
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 shadowPosition : TEXCOORD3;
    float3 worldPosition : TEXCOORD4;
    float3 localPosition : TEXCOORD5;
    float3 localNormal : TEXCOORD6;
    float3 worldNormal : TEXCOORD7;
};

cbuffer BasicUniforms : register(b0, space1) {
    column_major float4x4 modelViewProjection;
    column_major float4x4 shadowModelViewProjection;
    float4 color;
    float4 renderParams;
    column_major float4x4 modelMatrix;
};
struct MeshDecalUniform {
    float4 projection;
    float4 tint;
};

cbuffer ShadowUniforms : register(b0, space3) {
    float4 shadowParams;
    float4 materialParams;
    float4 faceTint;
    float4 faceUvScale;
    float4 meshDecalMinimumAndCount;
    float4 meshDecalMaximum;
    MeshDecalUniform meshDecals[8];
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    float4 position = float4(input.position, 1.0);
    output.position = renderParams.x > 0.5 ? mul(shadowModelViewProjection, position)
                                           : mul(modelViewProjection, position);
    if (renderParams.y > 0.5) {
        output.position.z = output.position.w;
    }
    output.color = color * input.color;
    output.uv = input.uv;
    output.shadowPosition = mul(shadowModelViewProjection, float4(input.position, 1.0));
    output.worldPosition = mul(modelMatrix, position).xyz;
    output.localPosition = input.position;
    output.localNormal = input.normal;
    const float3 modelX = mul(modelMatrix, float4(1.0, 0.0, 0.0, 0.0)).xyz;
    const float3 modelY = mul(modelMatrix, float4(0.0, 1.0, 0.0, 0.0)).xyz;
    const float3 modelZ = mul(modelMatrix, float4(0.0, 0.0, 1.0, 0.0)).xyz;
    output.worldNormal = cross(modelY, modelZ) * input.normal.x +
                         cross(modelZ, modelX) * input.normal.y +
                         cross(modelX, modelY) * input.normal.z;
    return output;
}

#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
Texture2D<float4> diffuseTexture : register(t0, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState diffuseSampler : register(s0, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
Texture2D<float> shadowTexture : register(t1, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState shadowSampler : register(s1, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
Texture2D<float4> surfaceTexture : register(t2, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState surfaceSampler : register(s2, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
Texture2D<float4> materialTexture : register(t3, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState materialSampler : register(s3, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
Texture2D<float4> meshDecalTexture : register(t4, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState meshDecalSampler : register(s4, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
Texture2D<float4> roughnessTexture : register(t5, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState roughnessSampler : register(s5, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
Texture2D<float4> metallicTexture : register(t6, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState metallicSampler : register(s6, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
Texture2D<float4> normalTexture : register(t7, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState normalSampler : register(s7, space2);

struct SdlLocalLight {
    float4 positionAndRange;
    float4 directionAndCutoff;
    float4 radiance;
};

cbuffer MaterialLighting : register(b1, space3) {
    float4 cameraAndLightCount;
    float4 sunDirection;
    float4 sunIrradiance;
    SdlLocalLight lights[8];
};

static const float PBR_PI = 3.14159265;

float3 safeNormalize(float3 value, float3 fallback) {
    const float lengthSquared = dot(value, value);
    return lengthSquared > 1e-8 ? value * rsqrt(lengthSquared) : fallback;
}

float pbrD(float ndh, float roughness) {
    float alpha = roughness * roughness;
    float a2 = alpha * alpha;
    float d = ndh * ndh * (a2 - 1.0) + 1.0;
    return a2 / max(PBR_PI * d * d, 1e-6);
}

float pbrG(float ndv, float ndl, float roughness) {
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return (ndv / (ndv * (1.0 - k) + k)) * (ndl / (ndl * (1.0 - k) + k));
}

float3 pbrF(float cosTheta, float3 f0) {
    return f0 + (1.0 - f0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 pbrDirect(float3 n, float3 v, float3 l, float3 irradiance,
                 float3 albedo, float metallic, float roughness, float3 f0) {
    const float ndl = max(dot(n, l), 0.0);
    if (ndl <= 0.0) return 0.0;
    const float ndv = max(dot(n, v), 1e-4);
    const float3 h = safeNormalize(v + l, n);
    const float3 f = pbrF(max(dot(v, h), 0.0), f0);
    const float specular = pbrD(max(dot(n, h), 0.0), roughness) *
                           pbrG(ndv, ndl, roughness);
    const float3 kd = (1.0 - f) * (1.0 - metallic);
    return (kd * albedo + specular * f * PBR_PI / max(4.0 * ndv * ndl, 1e-4)) * irradiance * ndl;
}

struct MaterialMapping {
    float2 uvX;
    float2 uvY;
    float2 uvZ;
    float3 weights;
};

MaterialMapping materialMapping(float3 position, float3 normal, float scale) {
    const float3 p = position / max(scale, 1e-3);
    const float3 weights = pow(abs(normal), 4.0);
    MaterialMapping map;
    map.uvX = float2(p.z, 1.0 - p.y);
    map.uvY = float2(p.x, 1.0 - p.z);
    map.uvZ = float2(p.x, 1.0 - p.y);
    map.weights = weights / max(dot(weights, float3(1.0, 1.0, 1.0)), 1e-4);
    return map;
}

float3 sampleMaterialMap(Texture2D<float4> textureMap, SamplerState mapSampler, MaterialMapping map) {
    return textureMap.Sample(mapSampler, map.uvX).rgb * map.weights.x +
           textureMap.Sample(mapSampler, map.uvY).rgb * map.weights.y +
           textureMap.Sample(mapSampler, map.uvZ).rgb * map.weights.z;
}

float3 materialNormal(float3 n, float strength, MaterialMapping map) {
    if (strength == 0.0) return n;
    float3 tx = normalTexture.Sample(normalSampler, map.uvX).xyz * 2.0 - 1.0;
    float3 ty = normalTexture.Sample(normalSampler, map.uvY).xyz * 2.0 - 1.0;
    float3 tz = normalTexture.Sample(normalSampler, map.uvZ).xyz * 2.0 - 1.0;
    tx.xy *= strength;
    ty.xy *= strength;
    tz.xy *= strength;
    tx = float3(tx.xy + n.zy, abs(tx.z) * n.x);
    ty = float3(ty.xy + n.xz, abs(ty.z) * n.y);
    tz = float3(tz.xy + n.xy, abs(tz.z) * n.z);
    return safeNormalize(tx.zyx * map.weights.x + ty.xzy * map.weights.y + tz.xyz * map.weights.z, n);
}

float4 sampleMeshDecal(float4 projection, float4 tint, float layer, float2 meshUv,
                       float3 localPosition, float3 localNormal) {
    const float mode = projection.w;
    float2 uv = float2(0.0, 0.0);
    float mask = 0.0;
    if (mode < -0.5) {
        const float radius = max(projection.z, 0.0001);
        const float2 delta = float2(meshUv.x, 1.0 - meshUv.y) - projection.xy;
        mask = 1.0 - smoothstep(radius * 0.85, radius, length(delta));
        uv = delta / radius * 0.5 + 0.5;
    } else {
        const float normalLength = length(localNormal);
        const float3 normal = normalLength > 0.0001 ? localNormal / normalLength : float3(0.0, 0.0, 0.0);
        const float3 faceNormals[6] = {
            float3(0.0, 0.0, -1.0), float3(0.0, 0.0, 1.0),
            float3(0.0, 1.0, 0.0), float3(0.0, -1.0, 0.0),
            float3(1.0, 0.0, 0.0), float3(-1.0, 0.0, 0.0)};
        const float3 extent = max(meshDecalMaximum.xyz - meshDecalMinimumAndCount.xyz, 0.0001);
        const float3 local = saturate((localPosition - meshDecalMinimumAndCount.xyz) / extent);
        const uint face = min((uint)mode, 5u);
        mask = smoothstep(0.35, 0.6, dot(normal, faceNormals[face]));
        if (face == 0u) uv = float2(1.0 - local.x, local.y);
        if (face == 1u) uv = float2(local.x, local.y);
        if (face == 2u) uv = float2(1.0 - local.x, local.z);
        if (face == 3u) uv = float2(1.0 - local.x, 1.0 - local.z);
        if (face == 4u) uv = float2(1.0 - local.z, local.y);
        if (face == 5u) uv = float2(local.z, local.y);
    }
    if (mask <= 0.0) return float4(0.0, 0.0, 0.0, 0.0);
    uv = saturate(uv);
    const float2 tile = float2(fmod(layer, 4.0), floor(layer / 4.0));
    const float2 pixel = clamp(uv * 256.0, 0.5, 255.5);
    const float2 atlasUv = (tile * 256.0 + float2(pixel.x, 256.0 - pixel.y)) / float2(1024.0, 512.0);
    float4 sample = meshDecalTexture.SampleLevel(meshDecalSampler, atlasUv, 0.0);
    sample.rgb *= tint.rgb;
    sample.a *= tint.a * mask;
    return sample;
}

float4 PSMain(VSOutput input) : SV_Target0 {
    float4 diffuse = diffuseTexture.Sample(diffuseSampler, input.uv);
    if (shadowParams.z >= 0.0 && diffuse.a < shadowParams.z) {
        discard;
    }
    float shadowFactor = 1.0;
    if (shadowParams.x > 0.5) {
        float3 shadowCoord = input.shadowPosition.xyz / input.shadowPosition.w;
        float2 shadowUv = float2(shadowCoord.x * 0.5 + 0.5, 0.5 - shadowCoord.y * 0.5);
        if (shadowUv.x >= 0.0 && shadowUv.x <= 1.0 && shadowUv.y >= 0.0 && shadowUv.y <= 1.0 &&
            shadowCoord.z >= 0.0 && shadowCoord.z <= 1.0) {
            float depth = shadowTexture.Sample(shadowSampler, shadowUv);
            shadowFactor = shadowCoord.z - 0.002 > depth ? 0.45 : 1.0;
        }
    }
    float4 result = input.color * diffuse;
    if (shadowParams.y > 1.5) {
        float2 faceUv = float2(input.uv.x * faceUvScale.x,
                               1.0 - (1.0 - input.uv.y) * faceUvScale.y);
        float4 overlay = surfaceTexture.Sample(surfaceSampler, faceUv);
        result.rgb = lerp(result.rgb, overlay.rgb * faceTint.rgb, overlay.a * faceTint.a);
    }
    const uint meshDecalCount = min((uint)meshDecalMinimumAndCount.w, 8u);
    for (uint i = 0u; i < meshDecalCount; ++i) {
        const float4 decal = sampleMeshDecal(meshDecals[i].projection, meshDecals[i].tint,
                                              (float)i, input.uv, input.localPosition,
                                              input.localNormal);
        result.rgb = lerp(result.rgb, decal.rgb, decal.a);
    }
    const uint materialFlags = (uint)max(meshDecalMaximum.w, 0.0);
    const bool usePbr = (materialFlags & 1u) != 0u;
    float3 normal = float3(0.0, 0.0, 1.0);
    MaterialMapping map = (MaterialMapping)0;
    if (usePbr || materialParams.y > 0.5) {
        const float3 geometric = cross(ddx(input.worldPosition), ddy(input.worldPosition));
        const float3 faceNormal = safeNormalize(geometric, normal);
        normal = safeNormalize(input.worldNormal, faceNormal);
        map = materialMapping(input.worldPosition, normal, materialParams.x);
        if (materialParams.y > 0.5) {
            result.rgb *= sampleMaterialMap(materialTexture, materialSampler, map);
        }
    }
    if (shadowParams.y > 0.5 && shadowParams.y < 1.5) {
        float4 overlay = surfaceTexture.Sample(surfaceSampler, input.uv);
        result.rgb = result.rgb * (1.0 - overlay.a) + overlay.rgb;
    }
    if (usePbr) {
        const float3 view = safeNormalize(cameraAndLightCount.xyz - input.worldPosition,
                                          float3(0.0, 0.0, 1.0));
        if (dot(normal, view) < 0.0) normal = -normal;
        float metallic = materialParams.z;
        float roughness = materialParams.w;
        if ((materialFlags & 4u) != 0u) {
            metallic *= sampleMaterialMap(metallicTexture, metallicSampler, map).r;
        }
        if ((materialFlags & 2u) != 0u) {
            roughness *= sampleMaterialMap(roughnessTexture, roughnessSampler, map).r;
        }
        if ((materialFlags & 8u) != 0u) {
            normal = materialNormal(normal, faceUvScale.w, map);
        }
        metallic = saturate(metallic);
        roughness = clamp(roughness, 0.045, 1.0);
        const float3 albedo = result.rgb;
        const float reflectance = saturate(faceUvScale.z);
        const float dielectric = 0.16 * reflectance * reflectance;
        const float3 f0 = lerp(float3(dielectric, dielectric, dielectric), albedo, metallic);
        float3 lightingResult = albedo * (1.0 - metallic) * 0.3;
        const float3 sunL = safeNormalize(sunDirection.xyz, float3(0.0, 1.0, 0.0));
        lightingResult += pbrDirect(normal, view, sunL,
                                    sunIrradiance.rgb * shadowFactor,
                                    albedo, metallic, roughness, f0);
        const uint lightCount = min((uint)max(cameraAndLightCount.w, 0.0), 8u);
        for (uint i = 0u; i < lightCount; ++i) {
            const float3 toLight = lights[i].positionAndRange.xyz - input.worldPosition;
            const float distanceToLight = length(toLight);
            const float3 lightDirection = toLight / max(distanceToLight, 1e-4);
            float attenuation = saturate(1.0 - distanceToLight /
                                         max(lights[i].positionAndRange.w, 1e-4));
            attenuation *= attenuation;
            if (lights[i].directionAndCutoff.w > -1.5) {
                const float cone = dot(-lightDirection,
                                       safeNormalize(lights[i].directionAndCutoff.xyz,
                                                     float3(0.0, -1.0, 0.0)));
                attenuation *= step(lights[i].directionAndCutoff.w, cone);
            }
            lightingResult += pbrDirect(normal, view, lightDirection,
                                        lights[i].radiance.rgb * attenuation,
                                        albedo, metallic, roughness, f0);
        }
        result.rgb = lightingResult;
    }
    if (shadowParams.w < 0.5) {
        result.a = input.color.a;
    }
    if (!usePbr) {
        result.rgb *= shadowFactor;
    }
    return result;
}
