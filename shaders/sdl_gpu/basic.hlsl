struct VSInput {
    float3 position : TEXCOORD0;
};

struct VSOutput {
    float4 position : SV_Position;
    nointerpolation float4 color : TEXCOORD0;
};

cbuffer BasicUniforms : register(b0, space1) {
    column_major float4x4 modelViewProjection;
    float4 color;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.position = mul(modelViewProjection, float4(input.position, 1.0));
    output.color = color;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target0 {
    return input.color;
}
