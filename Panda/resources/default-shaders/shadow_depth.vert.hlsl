// Depth-only shadow map pass (CSM), the general mesh vertex. Pancaking: casters closer than the
// light's near plane are pressed against it (z = max(z, 0), the light clip is orthographic ZO) —
// distant geometry on the sun's side casts a shadow without inflating the cascade depth.
#include "panda/vertex_ubo.hlsli"

struct VSInput {
    float3 position : POSITION;
    float2 texCoord : TEXCOORD0;
    float3 normal : NORMAL;
    float4 color : COLOR0;
    float light : TEXCOORD1;
    uint id : TEXCOORD2;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float2 fragTexCoord : TEXCOORD0; // the alpha-mask variant of the frag reads baseColorTexture
};

VSOutput main(VSInput input) {
    VSOutput output;
    float4 worldPos = mul(modelMtx, float4(input.position, 1.0));
    output.position = mul(projViewMtx, worldPos);
    output.position.z = max(output.position.z, 0.0);
    output.fragTexCoord = input.texCoord;
    return output;
}
