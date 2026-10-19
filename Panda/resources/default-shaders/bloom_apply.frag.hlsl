// Built-in bloom pyramid: final step — add the accumulated glow to the scene color in
// linear HDR (before the user postprocessing chain and the implicit tonemap).
// There are deliberately NO flares here: lens flare is a user pass of the chain (display
// space, after the user tonemap); the passes take the sun's screen position from
// POST_DATA.sunScreen, the occlusion — by sampling emissiveTexture at the sun's point.
cbuffer BLOOM_DATA {
    float4 sourceResolution; // xy — size of the bloom texture, px (unused)
    float4 params;           // y — intensity, z — 1/levels (normalization of the octave sum)
};

Texture2D colorTexture;
SamplerState colorTextureSampler;
Texture2D bloomTexture;
SamplerState bloomTextureSampler;

// The PS input mirrors the VS output including SV_POSITION: DXBC links the stages by registers.
struct PSInput {
    float4 position : SV_POSITION;
    float2 fragTexCoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target0 {
    float4 color = colorTexture.Sample(colorTextureSampler, input.fragTexCoord);
    float3 bloom = bloomTexture.Sample(bloomTextureSampler, input.fragTexCoord).rgb;
    return float4(color.rgb + bloom * (params.y * params.z), color.a);
}
