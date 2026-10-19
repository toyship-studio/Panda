// Depth-only shadow map (CSM) pass for the thin terrain vertex (TerrainVertex).
#include "panda/vertex_ubo.hlsli"

struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 splatUV : TEXCOORD0;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float2 fragTexCoord : TEXCOORD0;
};

VSOutput main(VSInput input) {
    VSOutput output;
    float4 worldPos = mul(modelMtx, float4(input.position, 1.0));
    output.position = mul(projViewMtx, worldPos);
    output.position.z = max(output.position.z, 0.0); // pancaking, see shadow_depth.vert
    output.fragTexCoord = input.splatUV;
    return output;
}
