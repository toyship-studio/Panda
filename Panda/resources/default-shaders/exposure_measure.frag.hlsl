// Auto exposure, step 1: measuring the frame luminance into a small target (64×64) — every
// pixel averages 2×2 spread samples (bilinear adds its own 2×2, ~4×4 of the frame in total).
Texture2D inputTexture;
SamplerState inputTextureSampler;

struct PSInput {
    float4 position : SV_POSITION;
    float2 fragTexCoord : TEXCOORD0;
};

float lum(float3 c) {
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float4 main(PSInput input) : SV_Target0 {
    const float2 o = float2(1.0 / 128.0, 1.0 / 128.0); // half a texel of the 64×64 target
    float value = 0.0;
    value += lum(inputTexture.Sample(inputTextureSampler, input.fragTexCoord + float2(-o.x, -o.y)).rgb);
    value += lum(inputTexture.Sample(inputTextureSampler, input.fragTexCoord + float2(o.x, -o.y)).rgb);
    value += lum(inputTexture.Sample(inputTextureSampler, input.fragTexCoord + float2(-o.x, o.y)).rgb);
    value += lum(inputTexture.Sample(inputTextureSampler, input.fragTexCoord + float2(o.x, o.y)).rgb);
    // Upper clamp: a lone sun in the frame must not drag the exposure to zero.
    return float4(min(value * 0.25, 64.0), 0.0, 0.0, 1.0);
}
