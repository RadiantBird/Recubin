struct VSInput {
    float3 position : TEXCOORD0;
    float4 color : TEXCOORD1;
};
struct VSOutput {
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
};
cbuffer ParticleParams : register(b0, space1) {
    float4x4 view;
    float4x4 projection;
};
VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.position = mul(projection, mul(view, float4(input.position, 1.0)));
    output.color = input.color;
    return output;
}
float4 PSMain(VSOutput input) : SV_Target0 { return input.color; }
