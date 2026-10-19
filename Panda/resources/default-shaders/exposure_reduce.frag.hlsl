// Auto exposure, step 2: reduction of the 64×64 measurement to 1×1 — the average over an 8×8
// grid of samples (bilinear at the centers of the 8×8 cells averages its own 2×2 → full
// coverage).
Texture2D inputTexture;
SamplerState inputTextureSampler;

struct PSInput {
    float4 position : SV_POSITION;
    float2 fragTexCoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target0 {
    float sum = 0.0;
    [unroll]
    for (int y = 0; y < 8; y++) {
        [unroll]
        for (int x = 0; x < 8; x++) {
            float2 uv = (float2(x, y) + 0.5) / 8.0;
            sum += inputTexture.Sample(inputTextureSampler, uv).r;
        }
    }
    return float4(sum / 64.0, 0.0, 0.0, 1.0);
}
