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
    float3 worldPosition : TEXCOORD4;
};

cbuffer BasicUniforms : register(b0, space1) {
    column_major float4x4 modelViewProjection;
    column_major float4x4 shadowModelViewProjection;
    float4 color;
    float4 renderParams;
    column_major float4x4 modelMatrix;
};
cbuffer ShadowUniforms : register(b0, space3) {
    float4 shadowParams;
    float4 materialParams;
    float4 faceTint;
    float4 faceUvScale;
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
