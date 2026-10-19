// A variant of the implicit final: the Narkowicz ACES approximation + sRGB gamma (the curve is
// 1:1 with user ACES chains — the Lab preview is finalized like the production world).
// Executed by the engine as tonemapOverride; do not add it to chains manually.
#include "panda/post_data.hlsli"

Texture2D inputTexture;
SamplerState inputTextureSampler;

struct PSInput {
    float4 position : SV_POSITION;
    float2 fragTexCoord : TEXCOORD0;
};

float3 acesToneMap(float3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 main(PSInput input) : SV_Target0 {
    float4 source = inputTexture.Sample(inputTextureSampler, input.fragTexCoord);
    float3 color = acesToneMap(source.rgb);
    color = pow(abs(color), 1.0 / 2.2);
    // Alpha passes through as is — material previews are drawn on a transparent background.
    return float4(color, source.a);
}
