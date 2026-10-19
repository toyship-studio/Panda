// Built-in bloom pyramid: up-step — tent-upsample of the lower (blurrier) level PLUS the
// stored level of this resolution. Each step accumulates one blur octave: the sum gives the
// production bloom look — bright tight core with a wide soft tail.
cbuffer BLOOM_DATA {
    float4 sourceResolution; // xy — size of the LOWER (being read) level, px
    float4 params;           // x — radius: spread of the tent taps in texels of the lower level
};

Texture2D sourceTexture; // lower level (result of the previous up step or the pyramid bottom)
SamplerState sourceTextureSampler;
Texture2D levelTexture;  // the level of this resolution from the downsample
SamplerState levelTextureSampler;

// The PS input mirrors the VS output including SV_POSITION: DXBC links the stages by registers.
struct PSInput {
    float4 position : SV_POSITION;
    float2 fragTexCoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target0 {
    float2 d = params.x / sourceResolution.xy;
    float2 uv = input.fragTexCoord;
    // Tent 3x3: weights 1-2-1 / 2-4-2 / 1-2-1 (sum 16).
    float3 lower = sourceTexture.Sample(sourceTextureSampler, uv).rgb * 4.0;
    lower += sourceTexture.Sample(sourceTextureSampler, uv + float2(-d.x, 0.0)).rgb * 2.0;
    lower += sourceTexture.Sample(sourceTextureSampler, uv + float2(d.x, 0.0)).rgb * 2.0;
    lower += sourceTexture.Sample(sourceTextureSampler, uv + float2(0.0, -d.y)).rgb * 2.0;
    lower += sourceTexture.Sample(sourceTextureSampler, uv + float2(0.0, d.y)).rgb * 2.0;
    lower += sourceTexture.Sample(sourceTextureSampler, uv + float2(-d.x, -d.y)).rgb;
    lower += sourceTexture.Sample(sourceTextureSampler, uv + float2(d.x, -d.y)).rgb;
    lower += sourceTexture.Sample(sourceTextureSampler, uv + float2(-d.x, d.y)).rgb;
    lower += sourceTexture.Sample(sourceTextureSampler, uv + float2(d.x, d.y)).rgb;
    float3 level = levelTexture.Sample(levelTextureSampler, uv).rgb;
    return float4(level + lower / 16.0, 1.0);
}
