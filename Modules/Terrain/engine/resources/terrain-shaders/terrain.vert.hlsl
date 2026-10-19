// Heightmap terrain (the thin TerrainVertex vertex): layer weights — the splatmap texture
// (sampled by splatUV in the fragment stage), the tiling UV of the layer textures — from the
// local position.
#include "panda/vertex_ubo.hlsli"

struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 splatUV : TEXCOORD0;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float2 tileUV : TEXCOORD2;  // tiling of the layer textures: 1 tile = 4 m (terrain local space)
    float2 splatUV : TEXCOORD3; // [0..1] across the map — splatmap sample
};

VSOutput main(VSInput input) {
    VSOutput output;
    float4 worldPos = mul(modelMtx, float4(input.position, 1.0));
    output.position = mul(projViewMtx, worldPos);
    output.worldPos = worldPos.xyz;
    output.worldNormal = normalize(mul((float3x3) modelMtx, input.normal));
    output.tileUV = input.position.xz / 4.0;
    output.splatUV = input.splatUV;
    return output;
}
