// SampleCmp through a comparison sampler: 1 — lit, 0 — in shadow.
// The "shadowMap"/"shadowMapSampler" pair is the Miren binding convention (<name>Sampler).
Texture2D shadowMap;
SamplerComparisonState shadowMapSampler;

struct PSInput {
    float4 position : SV_POSITION;
    float3 shadowCoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
    float lit = shadowMap.SampleCmpLevelZero(shadowMapSampler, input.shadowCoord.xy, input.shadowCoord.z);
    return float4(lit, lit, lit, 1.0);
}
