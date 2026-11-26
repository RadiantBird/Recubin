cbuffer SurfaceMarkVertexUniforms : register(b0, space1) {
    column_major float4x4 modelViewProjection;
    column_major float4x4 modelMatrix;
    float4 waveParams;
};

struct VSInput {
    float3 position : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 color : TEXCOORD2;
    float3 normal : TEXCOORD3;
};

struct VSOutput {
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
    float2 uv : TEXCOORD2;
};

VSOutput VSMain(VSInput input) {
    float3 local = input.position;
    if (waveParams.y > 0.5 && local.y > 0.0) {
        local.y += sin(waveParams.x * 1.5 + local.x * 4.0 + local.z * 4.0) * 0.06;
    }
    VSOutput output;
    output.position = mul(modelViewProjection, float4(local, 1.0));
    if (waveParams.z > 0.5) {
        output.position.z = output.position.z * 0.5 + output.position.w * 0.5;
    }
    output.position.z = output.position.z * 0.5 + output.position.w * 0.5;
    output.worldPosition = mul(modelMatrix, float4(local, 1.0)).xyz;
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
Texture2D<float4> markTexture : register(t0, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState markSampler : register(s0, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
Texture2D<float> markDepth : register(t1, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState markDepthSampler : register(s1, space2);

cbuffer SurfaceMarkFragmentUniforms : register(b0, space3) {
    column_major float4x4 worldToLocal;
    column_major float4x4 projection;
    float4 tint;
    float4 size;
};

float4 PSMain(VSOutput input) : SV_Target0 {
    const float3 local = mul(worldToLocal, float4(input.worldPosition, 1.0)).xyz;
    if (local.x < -size.x * 0.5 || local.x > size.x * 0.5 ||
        local.y < -size.y * 0.5 || local.y > size.y * 0.5 ||
        local.z > 0.0 || local.z < -size.z) discard;
    const float4 clipPosition = mul(projection, float4(input.worldPosition, 1.0));
    const float3 ndc = clipPosition.xyz / max(clipPosition.w, 1e-6);
    const float2 uv = ndc.xy * 0.5 + 0.5;
    if (any(uv < 0.0) || any(uv > 1.0)) discard;
    const float nearest = markDepth.Sample(markDepthSampler, uv);
    const float current = ndc.z * 0.5 + 0.5;
    if (current > nearest + 0.0015) discard;
    const float4 image = markTexture.Sample(markSampler, float2(uv.x, uv.y));
    const float alpha = image.a * tint.a * size.w;
    if (alpha <= 0.001) discard;
    return float4(image.rgb * tint.rgb, alpha);
}
