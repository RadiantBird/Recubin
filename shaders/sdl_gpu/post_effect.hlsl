struct VSOutput {
    // SPIRV-Cross emits the location inputs before fragment built-ins on DXIL.
    float2 uv : TEXCOORD0;
    float4 position : SV_Position;
};

VSOutput VSMain(uint vertexId : SV_VertexID) {
    const float2 positions[3] = {
        float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0)
    };
    const float2 coordinates[3] = {
        float2(0.0, 1.0), float2(2.0, 1.0), float2(0.0, -1.0)
    };
    VSOutput output;
    output.position = float4(positions[vertexId], 0.0, 1.0);
    output.uv = coordinates[vertexId];
    return output;
}

#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
Texture2D<float4> screenTexture : register(t0, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState screenTextureSampler : register(s0, space2);

cbuffer PostEffectUniforms : register(b0, space3) {
    float2 resolution;
    float time;
    float intensity;
    float param1;
    float param2;
    float effectType;
    float unused;
};

float3 sampleColor(float2 uv) { return screenTexture.Sample(screenTextureSampler, uv).rgb; }
float hash21(float2 p) { return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453); }

float3 applyCrt(float2 uv) {
    float2 centered = uv * 2.0 - 1.0;
    float dist = dot(centered, centered);
    float2 distorted = centered * (1.0 + param2 * dist) * 0.5 + 0.5;
    float mask = step(0.0, distorted.x) * step(0.0, distorted.y) *
                 step(distorted.x, 1.0) * step(distorted.y, 1.0);
    float3 color = sampleColor(distorted);
    float scan = sin(distorted.y * max(param1, 1.0) * 3.14159265);
    color *= 1.0 - 0.25 * (0.5 - 0.5 * scan);
    return color * mask;
}
float3 applyPixelize(float2 uv) {
    float2 cells = max(resolution / max(param1, 1.0), float2(1.0, 1.0));
    return sampleColor((floor(uv * cells) + 0.5) / cells);
}
float3 applyVhs(float2 uv) {
    float amount = max(param1, 0.0);
    float lineNoise = hash21(float2(floor(uv.y * resolution.y), time)) - 0.5;
    float3 color = sampleColor(uv + float2(lineNoise * amount * 0.02, 0.0));
    color += (hash21(uv * resolution + time) - 0.5) * amount * 0.25;
    return color;
}
float3 applyChromatic(float2 uv) {
    float2 offset = (uv - 0.5) * param1 * 0.02;
    return float3(sampleColor(uv + offset).r, sampleColor(uv).g, sampleColor(uv - offset).b);
}

float4 PSMain(VSOutput input) : SV_Target0 {
    float3 original = sampleColor(input.uv);
    float3 effected = original;
    int kind = (int)(effectType + 0.5);
    if (kind == 1) effected = applyCrt(input.uv);
    else if (kind == 2) effected = floor(original * max(param1, 1.0) + 0.5) / max(param1, 1.0);
    else if (kind == 3) effected = applyPixelize(input.uv);
    else if (kind == 4) {
        float gray = dot(original, float3(0.299, 0.587, 0.114));
        effected = lerp(float3(gray, gray, gray), original, param1);
    } else if (kind == 5) effected = applyVhs(input.uv);
    else if (kind == 6) effected = applyChromatic(input.uv);
    return float4(lerp(original, effected, saturate(intensity)), 1.0);
}
