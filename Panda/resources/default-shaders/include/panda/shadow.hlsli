// Cascaded sun shadows (CSM, context/lighting.md): the cascades lie as tiles along U of a single
// depth atlas (comparison sampler); a cascade is a slice by distance from the camera + the
// "world → (u, v, depth)" matrix of its tile (SCENE_LIGHTS). sampleShadow is the sun illumination
// 0..1 over the cascades, with a crossfade of adjacent cascades and a fade at Shadow Distance.
// Cascade tap: a 3×3 PCF tent on four bilinear compare taps (Castaño). Vegetation declares
// PANDA_SHADOW_SINGLE_TAP before the include — a single compare tap (hardware bilinear, 2×2
// texels): a soft shadow edge on foliage and grass is not discernible, while nine taps per pixel
// under overdraw are a noticeable share of the frag.
#include "panda/scene_lights.hlsli"

// Cascade shadow atlas (a system light input: the renderer binds it to shaders that declare it).
Texture2D shadowMap;
SamplerComparisonState shadowMapSampler;

// Atlas UV from the cascade tile UV: the tiles lie along U.
float2 cascadeAtlasUv(int cascade, float2 tileUv) {
    return float2((tileUv.x + (float)cascade) / shadowBias.z, tileUv.y);
}

#ifdef PANDA_SHADOW_SINGLE_TAP
// Shadow of a single cascade: one compare sample in its atlas tile; inside — the point fell
// into the tile (otherwise the caller takes the next cascade).
float sampleCascade(int cascade, float3 worldPos, float3 normal, float sinTheta, out bool inside) {
    float texel = shadowTexelWorld[cascade];
    float3 offsetPos = worldPos + normal * (shadowBias.x * texel * sinTheta);
    float3 coord = mul(shadowMatrices[cascade], float4(offsetPos, 1.0)).xyz;
    inside = all(coord.xy >= 0.0) && all(coord.xy <= 1.0);
    if (!inside) { return 1.0; }
    // Bilinear footprint (half a texel) stays inside the cascade tile (tile side = atlas height).
    coord.xy = clamp(coord.xy, 0.5 * shadowParams.y, 1.0 - 0.5 * shadowParams.y);
    float ref = coord.z - shadowBias.y * texel * shadowDepthScale[cascade];
    return shadowMap.SampleCmpLevelZero(shadowMapSampler, cascadeAtlasUv(cascade, coord.xy), ref);
}
#else
// Shadow of a single cascade: a 3×3 PCF tent on four bilinear compare taps (Castaño) in its
// atlas tile — a 4×4 texel footprint, like a 3×3 grid of nine taps, but twice as cheap and
// without boxy stair-steps; inside — the point fell into the tile (otherwise the caller takes
// the next one).
float sampleCascade(int cascade, float3 worldPos, float3 normal, float sinTheta, out bool inside) {
    float texel = shadowTexelWorld[cascade];
    float3 offsetPos = worldPos + normal * (shadowBias.x * texel * sinTheta);
    float3 coord = mul(shadowMatrices[cascade], float4(offsetPos, 1.0)).xyz;
    inside = all(coord.xy >= 0.0) && all(coord.xy <= 1.0);
    if (!inside) { return 1.0; }
    // The tent footprint (±2 texels) stays inside the cascade tile (tile side = atlas height).
    coord.xy = clamp(coord.xy, 2.5 * shadowParams.y, 1.0 - 2.5 * shadowParams.y);
    float ref = coord.z - shadowBias.y * texel * shadowDepthScale[cascade];
    // Tile texels (tile side = 1/shadowParams.y): the taps land between texels so that the
    // hardware bilinear weights add up to a tent; w0/w1 are the weights of the tap pairs,
    // o0/o1 are their offsets in texels from base.
    float2 uv = coord.xy / shadowParams.y;
    float2 base = floor(uv + 0.5);
    float2 st = uv + 0.5 - base;
    base -= 0.5;
    float2 w0 = 3.0 - 2.0 * st;
    float2 w1 = 1.0 + 2.0 * st;
    float2 o0 = (2.0 - st) / w0 - 1.0;
    float2 o1 = st / w1 + 1.0;
    float lit = 0.0;
    lit += w0.x * w0.y * shadowMap.SampleCmpLevelZero(
        shadowMapSampler, cascadeAtlasUv(cascade, (base + float2(o0.x, o0.y)) * shadowParams.y), ref);
    lit += w1.x * w0.y * shadowMap.SampleCmpLevelZero(
        shadowMapSampler, cascadeAtlasUv(cascade, (base + float2(o1.x, o0.y)) * shadowParams.y), ref);
    lit += w0.x * w1.y * shadowMap.SampleCmpLevelZero(
        shadowMapSampler, cascadeAtlasUv(cascade, (base + float2(o0.x, o1.y)) * shadowParams.y), ref);
    lit += w1.x * w1.y * shadowMap.SampleCmpLevelZero(
        shadowMapSampler, cascadeAtlasUv(cascade, (base + float2(o1.x, o1.y)) * shadowParams.y), ref);
    return lit / 16.0;
}
#endif

// Sun illumination 0..1 (1 — in light). worldNormal is the geometric normal (normal offset
// against acne). The cascade is the first one by distance from the camera whose border is beyond
// the point (if the point is outside its tile — the next one); in the last 20 % of the cascade
// range the next one is blended in — otherwise the edges would jump in resolution at the border.
// csmWeight is the CSM share: 1 in the shadow zone, → 0 at Distance and 0 without shadows.
float sampleShadow(float3 worldPos, float3 worldNormal, out float csmWeight) {
    csmWeight = 0.0;
    if (shadowParams.w <= 0.0) { return 1.0; }
    float toCamera = distance(worldPos, cameraPosition.xyz);
    float fade = saturate((shadowParams.z - toCamera) / max(shadowParams.z * 0.1, 0.001));
    if (fade <= 0.0) { return 1.0; }
    float3 normal = normalize(worldNormal);
    float cosTheta = saturate(dot(normal, normalize(directionalDirectionIntensity.xyz)));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    int cascadeCount = (int)shadowBias.z;
    float previousSplit = 0.0;
    [unroll]
    for (int i = 0; i < 4; i++) {
        if (i >= cascadeCount) { break; }
        float split = shadowSplits[i];
        if (toCamera > split) {
            previousSplit = split;
            continue;
        }
        bool inside;
        float lit = sampleCascade(i, worldPos, normal, sinTheta, inside);
        if (!inside) {
            previousSplit = split;
            continue;
        }
        float blendStart = split - 0.2 * (split - previousSplit);
        if (i + 1 < cascadeCount && toCamera > blendStart) {
            bool insideNext;
            float litNext = sampleCascade(i + 1, worldPos, normal, sinTheta, insideNext);
            if (insideNext) {
                lit = lerp(lit, litNext, saturate((toCamera - blendStart) / max(split - blendStart, 0.001)));
            }
        }
        csmWeight = fade;
        return 1.0 - shadowParams.w * (1.0 - lit);
    }
    return 1.0;
}
