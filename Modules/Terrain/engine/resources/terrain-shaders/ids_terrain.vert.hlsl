// IDS pass for the thin terrain vertex (TerrainVertex): the id comes as a material
// uniform (ids.frag), there is no vertex id.
#include "panda/vertex_ubo.hlsli"

struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 splatUV : TEXCOORD0;
};

struct VSOutput {
    float4 position : SV_POSITION;
    nointerpolation uint fragId : TEXCOORD2;
};

VSOutput main(VSInput input) {
    VSOutput output;
    float4 worldPos = mul(modelMtx, float4(input.position, 1.0));
    output.position = mul(projViewMtx, worldPos);
    output.fragId = 0u;
    return output;
}
