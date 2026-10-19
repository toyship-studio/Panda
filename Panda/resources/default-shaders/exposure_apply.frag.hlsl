// Auto exposure, step 4: application — the frame is multiplied by exposure = key / adaptedLum,
// clamped to [min, max]. From there on the pipeline (bloom, tonemap) sees an already exposed
// frame.
cbuffer EXPOSURE_DATA {
    float4 params; // x — key, y — min exposure, z — max exposure
};

Texture2D inputTexture;
SamplerState inputTextureSampler;
Texture2D adaptedTexture; // 1×1
SamplerState adaptedTextureSampler;

struct PSInput {
    float4 position : SV_POSITION;
    float2 fragTexCoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target0 {
    float4 color = inputTexture.Sample(inputTextureSampler, input.fragTexCoord);
    float adapted = adaptedTexture.Sample(adaptedTextureSampler, float2(0.5, 0.5)).r;
    float exposure = clamp(params.x / max(adapted, 1.e-3), params.y, params.z);
    return float4(color.rgb * exposure, color.a);
}
