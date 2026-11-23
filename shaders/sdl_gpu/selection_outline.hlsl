#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
Texture2D<float> selectionMask : register(t0, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState selectionSampler : register(s0, space2);

cbuffer OutlineUniforms : register(b0, space3) {
    float4 texelSize;
    float4 outlineColor;
};

struct PSInput {
    float2 uv : TEXCOORD0;
    float4 position : SV_Position;
};

float4 PSMain(PSInput input) : SV_Target0 {
    float center = selectionMask.Sample(selectionSampler, input.uv);
    float surrounding = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y) {
        [unroll] for (int x = -1; x <= 1; ++x) {
            float2 offset = float2(x, y) * texelSize.xy * texelSize.z;
            surrounding = max(surrounding, selectionMask.Sample(selectionSampler, input.uv + offset));
        }
    }
    float outline = surrounding * (1.0 - center);
    return float4(outlineColor.rgb, outline * outlineColor.a);
}
