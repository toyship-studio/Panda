#include "panda/vertex_ubo.hlsli"

struct VSInput {
    float3 position : POSITION;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 texCoord : TEXCOORD0;
};

VSOutput main(VSInput input) {
    VSOutput output;
    output.position = mul(projViewMtx, mul(modelMtx, float4(input.position, 1.0)));
    output.texCoord = input.position;
    return output;
}
