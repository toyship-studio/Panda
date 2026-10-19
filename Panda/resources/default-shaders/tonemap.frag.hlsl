// Implicit final pass: linear HDR scene -> display. Reinhard + sRGB gamma
// (the same curve the PBR shader used to apply inline before the HDR pipeline).
// Executed by the engine after the user postprocessing chain — never add it to a chain manually.
#include "panda/post_data.hlsli"

Texture2D inputTexture;
SamplerState inputTextureSampler;

// The PS input mirrors the VS output including SV_POSITION: DXBC links the stages by registers.
struct PSInput {
    float4 position : SV_POSITION;
    float2 fragTexCoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target0 {
    float4 source = inputTexture.Sample(inputTextureSampler, input.fragTexCoord);
    float3 color = source.rgb / (source.rgb + 1.0);
    color = pow(abs(color), 1.0 / 2.2);
    // Alpha passes through as is — material previews are drawn on a transparent background.
    return float4(color, source.a);
}
