#include "panda/vertex_ubo.hlsli"

struct VSInput {
    float3 position : POSITION;
};

float4 main(VSInput input) : SV_POSITION {
    float4 worldPos = mul(modelMtx, float4(input.position, 1.0));
    return mul(projViewMtx, worldPos);
}
