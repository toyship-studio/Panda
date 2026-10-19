// Auto exposure, step 3: eye adaptation — luminance creeps toward the measurement exponentially.
// k = 1 - exp(-dt * speed) is computed on the CPU (first frame — snap, k = 1).
cbuffer EXPOSURE_DATA {
    float4 params; // x — k (pull-up fraction per frame)
};

Texture2D previousTexture; // 1×1: adapted luminance of the previous frame
SamplerState previousTextureSampler;
Texture2D currentTexture;  // 1×1: measurement of this frame
SamplerState currentTextureSampler;

struct PSInput {
    float4 position : SV_POSITION;
    float2 fragTexCoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target0 {
    float previous = previousTexture.Sample(previousTextureSampler, float2(0.5, 0.5)).r;
    float current = currentTexture.Sample(currentTextureSampler, float2(0.5, 0.5)).r;
    return float4(lerp(previous, current, saturate(params.x)), 0.0, 0.0, 1.0);
}
