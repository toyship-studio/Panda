// Screen-space ambient occlusion, the blur (Panda::SsaoRenderer): the grain of the few slices
// and steps of ssao.frag averaged over a 5×5 neighborhood, a neighbor counted by how close its
// depth is — the occlusion of a surface does not leak over an edge onto another one. The
// intensity of the world is applied here.
// Output: r — the visible share 0..1, g — the view depth of the point (m).

cbuffer SSAO_PASS {
    float4 projection;
    float4 params; // x — radius (m), y — intensity, z — 1 / near, w — 1 / far
    float4 size;   // xy — the size of the depth in pixels, zw — of the target
};

Texture2D inputTexture;
SamplerState inputTextureSampler;

struct PSInput {
    float4 position : SV_POSITION;
    float2 fragTexCoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target0 {
    const float2 texel = 1.0 / size.zw;
    const float2 uv = input.fragTexCoord;
    const float2 center = inputTexture.SampleLevel(inputTextureSampler, uv, 0.0).rg;
    // The slope of the surface on screen (depth per texel), from the neighbor on the same
    // surface on each axis: a floor seen at a low angle changes its depth a lot from row to row
    // and is still one surface.
    const float right = inputTexture.SampleLevel(inputTextureSampler, uv + float2(texel.x, 0.0), 0.0).g - center.g;
    const float left = center.g - inputTexture.SampleLevel(inputTextureSampler, uv - float2(texel.x, 0.0), 0.0).g;
    const float down = inputTexture.SampleLevel(inputTextureSampler, uv + float2(0.0, texel.y), 0.0).g - center.g;
    const float up = center.g - inputTexture.SampleLevel(inputTextureSampler, uv - float2(0.0, texel.y), 0.0).g;
    const float2 slope = float2(abs(right) < abs(left) ? right : left, abs(down) < abs(up) ? down : up);
    float sum = 0.0;
    float weights = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            const float2 tap = inputTexture.SampleLevel(inputTextureSampler, uv + float2(x, y) * texel, 0.0).rg;
            // The same surface: the depth the slope predicts, within a few percent.
            const float expected = center.g + dot(float2(x, y), slope);
            const float weight = saturate(1.0 - abs(tap.g - expected) / (0.02 * center.g + 0.02));
            sum += tap.r * weight;
            weights += weight;
        }
    }
    const float visibility = sum / max(weights, 1.0e-4);
    return float4(pow(saturate(visibility), max(params.y, 0.0)), center.g, 0.0, 1.0);
}
