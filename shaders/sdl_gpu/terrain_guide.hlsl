cbuffer GuideUniforms : register(b0, space1) {
    column_major float4x4 viewProjection;
    float radius;
};

struct VSOutput {
    float4 position : SV_Position;
};

VSOutput VSMain(uint vertexId : SV_VertexID) {
    const uint segment = vertexId / 2;
    const uint side = vertexId % 2;
    const float angle = 6.28318530718 * (float(segment % 48) / 48.0);
    const float ringRadius = radius + (side == 0 ? -0.05 : 0.05);
    float2 localPosition = float2(cos(angle), sin(angle)) * ringRadius;
    VSOutput output;
    output.position = mul(viewProjection, float4(localPosition, 0.0, 1.0));
    return output;
}

float4 PSMain() : SV_Target0 {
    return float4(1.0, 0.85, 0.2, 1.0);
}
