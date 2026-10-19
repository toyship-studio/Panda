// Built-in bloom pyramid: box downsample step (4 bilinear taps cover 16 source texels —
// every source pixel contributes, no decimation shimmer). The first step reads the scene
// emissive buffer, subsequent steps read the previous pyramid level.
cbuffer BLOOM_DATA {
    float4 sourceResolution; // xy — size of the level being read, px
    float4 params;           // x — radius (upsample), y — intensity (apply), z — 1/levels
};

Texture2D sourceTexture;
SamplerState sourceTextureSampler;

// The PS input mirrors the VS output including SV_POSITION: DXBC links the stages by registers.
struct PSInput {
    float4 position : SV_POSITION;
    float2 fragTexCoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target0 {
    float2 halfTexel = 0.5 / sourceResolution.xy;
    float2 uv = input.fragTexCoord;
    float3 result = sourceTexture.Sample(sourceTextureSampler, uv + float2(-halfTexel.x, -halfTexel.y)).rgb;
    result += sourceTexture.Sample(sourceTextureSampler, uv + float2(halfTexel.x, -halfTexel.y)).rgb;
    result += sourceTexture.Sample(sourceTextureSampler, uv + float2(-halfTexel.x, halfTexel.y)).rgb;
    result += sourceTexture.Sample(sourceTextureSampler, uv + float2(halfTexel.x, halfTexel.y)).rgb;
    return float4(result * 0.25, 1.0);
}
