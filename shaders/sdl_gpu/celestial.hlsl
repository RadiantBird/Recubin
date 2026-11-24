// SDL GPU celestial bodies and sky flare overlays.
// The CPU supplies all positions in target NDC, so this pass does not depend
// on a particular camera projection convention.

struct CelestialUniforms {
    float4 placement;       // center NDC xy, half-size NDC xy
    float4 occlusion;       // disc half-size NDC xy, depth cutoff, unused
    float4 color;
    float4 directionAndMode; // view direction xyz, mode (disc, phase, flare, veil)
    float4 projection;      // tanHalfFovX, tanHalfFovY, inverse width, inverse height
    float4 glow;             // extent, intensity, radius, unused
    float4 spikes;           // intensity, length, rotation, count
    float4 appearance;       // phase, phase rotation, earthshine, unused
    float4 veil;              // intensity, falloff, unused, unused
};
ConstantBuffer<CelestialUniforms> vertex : register(b0, space1);
ConstantBuffer<CelestialUniforms> fragment : register(b0, space3);

#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
Texture2D<float> sceneDepthTexture : register(t0, space0);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState sceneDepthSampler : register(s0, space0);

struct VSOutput {
    float4 position : SV_Position;
    float2 local : TEXCOORD0;
    float3 ray : TEXCOORD1;
    nointerpolation float visibility : TEXCOORD2;
};

float sceneDepthAt(float2 ndc) {
    const float2 uv = float2(ndc.x * 0.5 + 0.5, 1.0 - (ndc.y * 0.5 + 0.5));
    return sceneDepthTexture.SampleLevel(sceneDepthSampler, uv, 0.0);
}

float visibleDisc() {
    if (vertex.directionAndMode.w < 1.5) return 1.0;
    float visible = 0.0;
    float total = 0.0;
    [unroll]
    for (int y = -2; y <= 2; ++y) {
        [unroll]
        for (int x = -2; x <= 2; ++x) {
            if (x * x + y * y > 4) continue;
            const float2 offset = float2(x, y) * 0.5;
            const float2 sampleNdc = vertex.placement.xy + offset * vertex.occlusion.xy;
            if (abs(sampleNdc.x) > 1.0 || abs(sampleNdc.y) > 1.0) {
                visible += 1.0;
            } else {
                const float depth = sceneDepthAt(sampleNdc);
                visible += depth >= vertex.occlusion.z ? 1.0 : 0.0;
            }
            total += 1.0;
        }
    }
    return visible / max(total, 1.0);
}

VSOutput VSMain(uint vertexId : SV_VertexID) {
    const float2 positions[6] = {
        float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0),
        float2(-1.0, -1.0), float2(1.0, 1.0), float2(-1.0, 1.0)
    };
    const float2 local = positions[vertexId];
    VSOutput output;
    output.local = local;
    output.ray = float3(local.x * vertex.projection.x, local.y * vertex.projection.y, 1.0);
    if (vertex.directionAndMode.w > 2.5) {
        output.position = float4(local, 0.0, 1.0);
    } else {
        output.position = float4(vertex.placement.xy + local * vertex.placement.zw, 0.0, 1.0);
    }
    output.visibility = visibleDisc();
    return output;
}

#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
Texture2D<float> fragmentSceneDepthTexture : register(t0, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState fragmentSceneDepthSampler : register(s0, space2);

float fragmentOcclusion(float2 position) {
    const float2 uv = position * fragment.projection.zw;
    const float2 pixel = float2(fragment.projection.z, fragment.projection.w) * 2.0;
    const float center = fragmentSceneDepthTexture.Sample(fragmentSceneDepthSampler, uv);
    const float right = fragmentSceneDepthTexture.Sample(fragmentSceneDepthSampler, uv + float2(pixel.x, 0.0));
    const float left = fragmentSceneDepthTexture.Sample(fragmentSceneDepthSampler, uv - float2(pixel.x, 0.0));
    const float down = fragmentSceneDepthTexture.Sample(fragmentSceneDepthSampler, uv + float2(0.0, pixel.y));
    const float up = fragmentSceneDepthTexture.Sample(fragmentSceneDepthSampler, uv - float2(0.0, pixel.y));
    return (center < fragment.occlusion.z ? 1.0 : 0.0) +
           (right < fragment.occlusion.z ? 1.0 : 0.0) +
           (left < fragment.occlusion.z ? 1.0 : 0.0) +
           (down < fragment.occlusion.z ? 1.0 : 0.0) +
           (up < fragment.occlusion.z ? 1.0 : 0.0);
}

float4 PSMain(VSOutput input) : SV_Target0 {
    const float mode = fragment.directionAndMode.w;
    const float visibility = saturate(input.visibility);
    const float2 local = input.local;
    const float localRadius = length(local);
    if (mode > 2.5) {
        const float facing = max(dot(normalize(input.ray), normalize(fragment.directionAndMode.xyz)), 0.0);
        const float veilValue = fragment.veil.x * pow(facing, fragment.veil.y) * fragment.color.a * visibility;
        return float4(lerp(float3(1.0, 1.0, 1.0), fragment.color.rgb, 0.5) * veilValue, 1.0);
    }
    if (mode < 1.5) {
        const float r2 = localRadius * localRadius;
        if (r2 >= 1.0) discard;
        const float phaseAngle = 6.28318530718 * fragment.appearance.x;
        float3 light = float3(sin(phaseAngle), 0.0, -cos(phaseAngle));
        const float c = cos(fragment.appearance.y);
        const float s = sin(fragment.appearance.y);
        light.xy = float2(c * light.x - s * light.y, s * light.x + c * light.y);
        const float3 normal = float3(local, sqrt(max(1.0 - r2, 0.0)));
        const float lit = smoothstep(-0.04, 0.04, dot(normal, light));
        const float3 outputColor = lerp(fragment.color.rgb * 0.35, fragment.color.rgb, lit);
        const float edge = 1.0 - smoothstep(0.97, 1.0, localRadius);
        const float alpha = lerp(fragment.appearance.z, 1.0, lit) * edge;
        return float4(outputColor, alpha * fragment.color.a * (1.0 - fragmentOcclusion(input.position.xy) * 0.2));
    }
    if (localRadius >= 1.0) discard;
    const float radius = localRadius * max(fragment.glow.x, 1.0);
    const float occluded = saturate(fragmentOcclusion(input.position.xy) * 0.2);
    const float glowRadius = max(fragment.glow.z, 1.0);
    const float glowValue = fragment.glow.y * pow(1.0 - smoothstep(0.0, glowRadius, radius), 2.0);
    const float count = max(fragment.spikes.w, 2.0);
    const float sector = 6.28318530718 / count;
    const float angle = atan2(local.y, local.x) - fragment.spikes.z;
    const float wrapped = angle + 0.5 * sector - floor((angle + 0.5 * sector) / sector) * sector;
    const float folded = abs(wrapped - 0.5 * sector);
    const float along = radius * cos(folded);
    const float across = radius * sin(folded);
    const float t = saturate(along / max(fragment.spikes.y, 1.0));
    const float sigma = 0.012 + 0.07 * (1.0 - t);
    const float spikeValue = fragment.spikes.x * exp(-(across * across) / (2.0 * sigma * sigma)) *
                             (1.0 - t) * (1.0 - t) / (1.0 + 0.15 * along);
    const float light = glowValue * (1.0 - occluded + occluded * 0.35 * visibility) + spikeValue * (1.0 - occluded);
    return float4(fragment.color.rgb * light * fragment.color.a, 1.0);
}
