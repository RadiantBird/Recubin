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
