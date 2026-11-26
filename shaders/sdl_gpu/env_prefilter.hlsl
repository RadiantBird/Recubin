// GGX prefilter pass for a cubemap face. The CPU records one pass per face/mip.
struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID) {
    const float2 coordinates[3] = {
        float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0)
    };
    VSOutput output;
    output.position = float4(coordinates[vertexId], 0.0, 1.0);
    output.uv = coordinates[vertexId] * 0.5 + 0.5;
    return output;
}

#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
TextureCube<float4> sourceTexture : register(t0, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]]
#endif
SamplerState sourceSampler : register(s0, space2);

cbuffer PrefilterUniforms : register(b0, space3) {
    float roughness;
    float face;
    float targetSize;
    float sourceSize;
};

static const float PI = 3.14159265;
static const uint SAMPLE_COUNT = 128u;

float radicalInverseVdc(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 hammersley(uint index, uint count) {
    return float2(float(index) / float(count), radicalInverseVdc(index));
}

float3 faceDirection(int faceIndex, float2 uv) {
    float2 p = uv * 2.0 - 1.0;
    if (faceIndex == 0) return float3(1.0, -p.y, -p.x);
    if (faceIndex == 1) return float3(-1.0, -p.y, p.x);
    if (faceIndex == 2) return float3(p.x, 1.0, p.y);
    if (faceIndex == 3) return float3(p.x, -1.0, -p.y);
    if (faceIndex == 4) return float3(p.x, -p.y, 1.0);
    return float3(-p.x, -p.y, -1.0);
}

float3 importanceSampleGgx(float2 xi, float3 normal, float alpha) {
    const float phi = 2.0 * PI * xi.x;
    const float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (alpha * alpha - 1.0) * xi.y));
    const float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    const float3 halfVector = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    const float3 up = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(up, normal));
    const float3 bitangent = cross(normal, tangent);
    return normalize(tangent * halfVector.x + bitangent * halfVector.y + normal * halfVector.z);
}

float distributionGgx(float nDotH, float value) {
    const float alpha = value * value;
    const float alphaSquared = alpha * alpha;
    const float denominator = nDotH * nDotH * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(PI * denominator * denominator, 1e-6);
}

float4 PSMain(VSOutput input) : SV_Target0 {
    const float3 normal = normalize(faceDirection((int)(face + 0.5), input.uv));
    if (roughness < 0.001) {
        return float4(sourceTexture.SampleLevel(sourceSampler, normal, 0.0).rgb, 1.0);
    }
    const float3 view = normal;
    float3 sum = 0.0;
    float totalWeight = 0.0;
    const float texelSolidAngle = 4.0 * PI / (6.0 * sourceSize * sourceSize);
    for (uint index = 0u; index < SAMPLE_COUNT; ++index) {
        const float2 xi = hammersley(index, SAMPLE_COUNT);
        const float3 halfVector = importanceSampleGgx(xi, normal, roughness * roughness);
        const float3 light = normalize(2.0 * dot(view, halfVector) * halfVector - view);
        const float nDotL = max(dot(normal, light), 0.0);
        if (nDotL <= 0.0) continue;
        const float nDotH = max(dot(normal, halfVector), 0.0);
        const float hDotV = max(dot(halfVector, view), 0.0);
        const float pdf = distributionGgx(nDotH, roughness) * nDotH / (4.0 * hDotV) + 1e-4;
        const float sampleSolidAngle = 1.0 / (float(SAMPLE_COUNT) * pdf + 1e-4);
        const float lod = 0.5 * log2(sampleSolidAngle / texelSolidAngle);
        sum += sourceTexture.SampleLevel(sourceSampler, light, max(lod, 0.0)).rgb * nDotL;
        totalWeight += nDotL;
    }
    return float4(sum / max(totalWeight, 1e-4), 1.0);
}
