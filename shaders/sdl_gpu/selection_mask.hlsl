struct VSOutput {
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 shadowPosition : TEXCOORD3;
};

cbuffer SelectionUniforms : register(b0, space3) {
    float4 shadowParams;
};

#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
Texture2D<float4> diffuseTexture : register(t0, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState diffuseSampler : register(s0, space2);

float4 PSMain(VSOutput input) : SV_Target0 {
    if (shadowParams.z >= 0.0) {
        clip(diffuseTexture.Sample(diffuseSampler, input.uv).a - shadowParams.z);
    }
    return float4(1.0, 1.0, 1.0, 1.0);
}
