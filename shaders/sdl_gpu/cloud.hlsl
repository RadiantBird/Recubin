// SDL GPU cloud layer. The CPU supplies a camera-independent horizontal quad;
// wind scrolling and coverage are applied in the fragment stage.

cbuffer CloudVertexUniforms : register(b0, space1) {
    column_major float4x4 view;
    column_major float4x4 projection;
    column_major float4x4 model;
};

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
    output.uv = input.uv;
    output.position = mul(projection, mul(view, mul(model, float4(input.position, 1.0))));
    return output;
}

#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
Texture2D<float4> cloudTexture : register(t0, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState cloudSampler : register(s0, space2);

cbuffer CloudFragmentUniforms : register(b0, space3) {
    float4 cloudColor;
    float2 windOffset;
    float cloudCover;
    float cloudDensity;
};

float4 PSMain(VSOutput input) : SV_Target0 {
    const float noise = cloudTexture.Sample(cloudSampler, input.uv - windOffset).r;
    const float alpha = smoothstep(1.0 - saturate(cloudCover), 1.0, noise) *
                        saturate(cloudDensity) * cloudColor.a;
    return float4(cloudColor.rgb, alpha);
}
