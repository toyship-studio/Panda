// Depth-only pass (shadow map): position only, no outputs other than SV_POSITION.
cbuffer UBO_VERT {
    float4x4 projViewMtx;
};

struct VSInput {
    float3 position : POSITION;
};

struct VSOutput {
    float4 position : SV_POSITION;
};

VSOutput main(VSInput input) {
    VSOutput output;
    output.position = mul(projViewMtx, float4(input.position, 1.0));
    return output;
}
