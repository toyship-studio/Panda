// Shadow receiver: shadowMtx maps world space straight into (u, v, depth) of the shadow map — the
// v flip and the backend's depth range are handled on the CPU, the shader does not depend on the API.
cbuffer UBO_VERT {
    float4x4 projViewMtx;
    float4x4 shadowMtx;
};

struct VSInput {
    float3 position : POSITION;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 shadowCoord : TEXCOORD0;
};

VSOutput main(VSInput input) {
    VSOutput output;
    float4 worldPos = float4(input.position, 1.0);
    output.position = mul(projViewMtx, worldPos);
    output.shadowCoord = mul(shadowMtx, worldPos).xyz;
    return output;
}
