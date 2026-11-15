// Depth-only shadow map (CSM) pass for the thin terrain vertex (TerrainVertex).
#include "panda/draw.hlsli"

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
    // The pancake is for the orthographic eye of the sun (w is exactly 1 there): the eye of a
    // lamp is a perspective, and a caster behind it must be clipped, not pressed to the plane.
    if (output.position.w == 1.0) { output.position.z = max(output.position.z, 0.0); }
    output.fragTexCoord = input.splatUV;
    return output;
}
