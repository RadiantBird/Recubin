struct VSInput {
    float3 position : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.position = float4(input.position, 1.0f);
    output.uv = input.uv;
    return output;
}

#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
Texture2D<float4> sourceTexture : register(t0, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState sourceSampler : register(s0, space2);

float4 PSMain(VSOutput input) : SV_Target0 {
    return sourceTexture.Sample(sourceSampler, input.uv);
}
