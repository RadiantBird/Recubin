struct VSInput {
    float3 position : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 color : TEXCOORD2;
};

struct VSOutput {
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 shadowPosition : TEXCOORD3;
};

cbuffer BasicUniforms : register(b0, space1) {
    column_major float4x4 modelViewProjection;
    column_major float4x4 shadowModelViewProjection;
    float4 color;
    float4 renderParams;
};
cbuffer ShadowUniforms : register(b0, space3) {
    float4 shadowParams;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    float4 position = float4(input.position, 1.0);
    output.position = renderParams.x > 0.5 ? mul(shadowModelViewProjection, position)
                                           : mul(modelViewProjection, position);
    output.color = color * input.color;
    output.uv = input.uv;
    output.shadowPosition = mul(shadowModelViewProjection, float4(input.position, 1.0));
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

float4 PSMain(VSOutput input) : SV_Target0 {
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
    float4 result = input.color * diffuseTexture.Sample(diffuseSampler, input.uv);
    if (shadowParams.y > 0.5) {
        float4 overlay = surfaceTexture.Sample(surfaceSampler, input.uv);
        result.rgb = result.rgb * (1.0 - overlay.a) + overlay.rgb;
    }
    result.rgb *= shadowFactor;
    return result;
}
