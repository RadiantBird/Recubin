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
    output.color = color * input.color;
    output.uv = input.uv;
    output.shadowPosition = mul(shadowModelViewProjection, float4(input.position, 1.0));
    output.worldPosition = mul(modelMatrix, position).xyz;
    output.localPosition = input.position;
    output.localNormal = input.normal;
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
    if (materialParams.y > 0.5) {
        float3 normal = normalize(cross(ddx(input.worldPosition), ddy(input.worldPosition)));
        float3 weights = pow(abs(normal), 4.0);
        weights /= max(dot(weights, float3(1.0, 1.0, 1.0)), 0.0001);
        float3 materialPosition = input.worldPosition / max(materialParams.x, 0.001);
        float4 materialColor = materialTexture.Sample(materialSampler,
            float2(materialPosition.z, 1.0 - materialPosition.y)) * weights.x;
        materialColor += materialTexture.Sample(materialSampler,
            float2(materialPosition.x, 1.0 - materialPosition.z)) * weights.y;
        materialColor += materialTexture.Sample(materialSampler,
            float2(materialPosition.x, 1.0 - materialPosition.y)) * weights.z;
        result.rgb *= materialColor.rgb;
    }
    if (shadowParams.w < 0.5) {
        result.a = input.color.a;
    }
    if (shadowParams.y > 0.5 && shadowParams.y < 1.5) {
        float4 overlay = surfaceTexture.Sample(surfaceSampler, input.uv);
        result.rgb = result.rgb * (1.0 - overlay.a) + overlay.rgb;
    }
    result.rgb *= shadowFactor;
    return result;
}
