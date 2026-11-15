// PANDA_SHADOWS — cascaded sun shadows (shader feature `shadows`, context/lighting.md): the
// cascades lie as tiles along U of the single depth atlas shadowMap (comparison sampler); a
// cascade is a slice by distance from the camera + the "world → (u, v, depth)" matrix of its tile.
// sampleShadow is the sun illumination 0..1 over the cascades, with a crossfade of adjacent
// cascades and a fade at Shadow Distance. The feature needs `lights` (the sun) and `view` (the
// camera). The edge of a shadow is soft by the distance to its caster (shadowSoftness: the sun
// is a disc, not a point): casters are searched around the point, their mean distance gives the
// width of the penumbra, and the cascade is filtered that wide — sharp at a contact, soft far
// from it. With a softness of 0 the filter is a fixed 3×3 PCF tent on four bilinear compare
// taps (Castaño). Vegetation declares PANDA_SHADOW_SINGLE_TAP before the include — a single
// compare tap (hardware bilinear, 2×2 texels): a soft shadow edge on foliage and grass is not
// discernible, while many taps per pixel under overdraw are a noticeable share of the frag. With
// the feature off the include is a compile error; a lit shader takes panda/lighting.hlsli, which
// degrades without it.
#include "panda/blocks/shadows.hlsli"
#include "panda/view.hlsli"
#include "panda/lights.hlsli"

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
static const int SHADOW_SEARCH_TAPS = 12;
static const int SHADOW_FILTER_TAPS = 16;

// A tap of a disc of taps that covers it evenly (Vogel), turned by `turn` radians.
float2 shadowDiscTap(int index, int count, float turn) {
    const float radius = sqrt((float(index) + 0.5) / float(count));
    const float angle = float(index) * 2.39996323 + turn;
    return float2(cos(angle), sin(angle)) * radius;
}

// The soft shadow of a cascade (shadowSoftness.x > 0). `coord` — the point in the tile (uv,
// depth), `ref` — its biased depth, `plane` — the slope of the receiver in the cascade (depth
// per uv): a tap aside of the point is compared with the depth the receiver itself has there, so
// a wide filter does not make a slope shade itself.
float softCascade(int cascade, float3 coord, float ref, float2 plane, float3 worldPos) {
    const float texel = shadowTexelWorld[cascade];
    const float uvPerMeter = shadowParams.y / texel;
    const float2 atlasSize = 1.0 / shadowParams.xy;
    const float2 edge = float2(0.5 * shadowParams.y, 1.0 - 0.5 * shadowParams.y);
    // The disc turns from pixel to pixel: the few taps become grain instead of bands.
    const float3 seed = frac(worldPos * float3(0.1031, 0.1030, 0.0973));
    const float turn = frac(52.9829189 * frac(dot(seed, float3(127.1, 311.7, 74.7)))) * 6.2831853;

    // The casters around the point, as far as the widest penumbra reaches.
    const float searchRadius = shadowSoftness.y * uvPerMeter;
    float casterDepth = 0.0;
    float casters = 0.0;
    for (int i = 0; i < SHADOW_SEARCH_TAPS; i++) {
        const float2 offset = shadowDiscTap(i, SHADOW_SEARCH_TAPS, turn) * searchRadius;
        const float2 uv = clamp(coord.xy + offset, edge.x, edge.y);
        const float depth = shadowMap.Load(int3(int2(cascadeAtlasUv(cascade, uv) * atlasSize), 0)).r;
        if (depth < ref + dot(offset, plane)) {
            casterDepth += depth;
            casters += 1.0;
        }
    }
    if (casters < 0.5) { return 1.0; }
    // The penumbra by the mean distance to the casters; never narrower than the filter that
    // hides the texels of the cascade.
    const float distanceToCaster = max(coord.z - casterDepth / casters, 0.0) / shadowDepthScale[cascade];
    const float penumbra = clamp(distanceToCaster * shadowSoftness.x, 1.25 * texel, max(shadowSoftness.y, 1.25 * texel));
    const float filterRadius = penumbra * uvPerMeter;
    float lit = 0.0;
    for (int j = 0; j < SHADOW_FILTER_TAPS; j++) {
        const float2 offset = shadowDiscTap(j, SHADOW_FILTER_TAPS, turn) * filterRadius;
        const float2 uv = clamp(coord.xy + offset, edge.x, edge.y);
        lit += shadowMap.SampleCmpLevelZero(shadowMapSampler, cascadeAtlasUv(cascade, uv), ref + dot(offset, plane));
    }
    return lit / float(SHADOW_FILTER_TAPS);
}

// Shadow of a single cascade in its atlas tile: the soft shadow, or with a softness of 0 a 3×3
// PCF tent on four bilinear compare taps (Castaño) — a 4×4 texel footprint, like a 3×3 grid of
// nine taps, but twice as cheap and without boxy stair-steps; inside — the point fell into the
// tile (otherwise the caller takes the next one).
float sampleCascade(int cascade, float3 worldPos, float3 normal, float sinTheta, out bool inside) {
    float texel = shadowTexelWorld[cascade];
    float3 offsetPos = worldPos + normal * (shadowBias.x * texel * sinTheta);
    float3 coord = mul(shadowMatrices[cascade], float4(offsetPos, 1.0)).xyz;
    inside = all(coord.xy >= 0.0) && all(coord.xy <= 1.0);
    if (!inside) { return 1.0; }
    if (shadowSoftness.x > 0.0) {
        // The receiver as a plane of the cascade: the normal in tile space is (uv per meter,
        // uv per meter, depth per meter) times its components along the axes of the light, so
        // depth changes along u by -(depth per meter / uv per meter)^2 * n.u / n.depth. A
        // surface edge-on to the sun is capped: its slope has no end.
        const float3 tileNormal = mul(shadowMatrices[cascade], float4(normal, 0.0)).xyz;
        const float scale = shadowDepthScale[cascade] * texel / shadowParams.y;
        const float facing = abs(tileNormal.z) / shadowDepthScale[cascade];
        const float2 plane = -scale * scale * tileNormal.xy * sign(tileNormal.z) /
                             (max(facing, 0.15) * shadowDepthScale[cascade]);
        const float softRef = coord.z - shadowBias.y * texel * shadowDepthScale[cascade];
        return softCascade(cascade, coord, softRef, plane, worldPos);
    }
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
    float cosTheta = saturate(dot(normal, normalize(sunDirectionIntensity.xyz)));
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
