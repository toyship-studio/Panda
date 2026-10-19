// Crown vertex shader of foliage clusters: default_lit.vert + a weak gust sway of the
// tuft cards (the same windGustEnergy energy that drives the flutter in the veg_crown frag:
// the crown breathes in unison, the strength layers are shared).
// The sway weight is baked into COLOR_0.a (the branch base is static, the tips move);
// the card is displaced AS A WHOLE (the tuft seed id is shared by its vertices) — the quad
// does not tear.
// There is NO high-frequency flutter in the vertices (it is per-leaf and lives in the frag);
// there is also no static "lean" from the wind strength — the sway is around a slightly
// blown-off neutral (the previous vertex wind "laid down" the vegetation with a static offset).
// MATERIAL_FIELDS is a full copy of the frag layout (a single std140 contract per program,
// the precedent for vertex material fields is grass/flora_billboard). windStrength = 0
// (calm, impostor bake) disables the branch: parity with default_lit.vert.

#include "panda/vertex_ubo.hlsli"

cbuffer MATERIAL_FIELDS {
    float4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    float4 emissiveFactor;
    uint alphaMode;
    float alphaCutoff;
    uint doubleSided;
    float emissiveIntensity;
    float viewFade;
    float windStrength;
    float windGustiness;
    float canopyNormal;  // leaf lighting basis: 0 — card plane, 1 — crown volume normal
    float translucency;  // leaf translucency: share of sun through the blade when the sun is behind the leaf
};

#include "panda/panda_fields.hlsli"

struct VSInput {
    float3 position : POSITION;
    float2 texCoord : TEXCOORD0;
    float3 normal : NORMAL;
    float4 color : COLOR0;
    float light : TEXCOORD1;
    uint id : TEXCOORD2;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float2 fragTexCoord : TEXCOORD2;
    float fragLight : TEXCOORD4;
    // Per-instance tone jitter: written by the instanced variant, white for a single tree.
    nointerpolation float3 fragTint : TEXCOORD6;
    // Gust energy (wind layers 1+2) — smooth over the world, computed here once
    // per card, not per pixel (the frag keeps only the per-leaf flutter for itself).
    float fragGust : TEXCOORD7;
};

// 1D value noise with a period — the same as in the frag (see veg_crown.frag).
float windNoise(float t, float period) {
    const float f = frac(t);
    const float cell = fmod(floor(t), period);
    const float a = frac(sin(cell * 12.9898) * 43758.5453);
    const float b = frac(sin(fmod(cell + 1.0, period) * 12.9898) * 43758.5453);
    return lerp(a, b, f * f * (3.0 - 2.0 * f));
}

// Layers 1+2 of the scene wind strength — AS IN THE FRAG: the gust that tilts the cards
// and the gust that ignites the flashes are one and the same energy.
float windGustEnergy(float2 worldXZ) {
    const float envelope = lerp(0.5, smoothstep(0.15, 0.85,
        windNoise(sceneWind.w / 13.0, 231.0) * 0.65 +
        windNoise(sceneWind.w / 33.0 + 7.7, 91.0) * 0.35), windGustiness);
    const float along = dot(worldXZ, sceneWind.xy) - sceneWind.z;
    const float wave = windNoise(along / 11.0, 273.0) * 0.6 +
                       windNoise(along / 21.0 + 61.8, 143.0) * 0.4;
    const float local = (smoothstep(0.2, 0.9, wave) - 0.5) * (0.4 + 0.6 * windGustiness);
    return saturate(envelope + local);
}

// Card displacement by wind (world coordinates). The normal is not touched: the card
// is displaced as a rigid body, the leaf tilt is done by the frag.
float3 crownWindOffset(float gust, float windWeight, uint id) {
    // Layer 3 is a per-card oscillation (the phase from the tuft seed): the crown does not move
    // as a monolith, the swing is larger in a gust. Wrapping the id is hash precision hygiene on
    // large trees.
    const float cardSway =
        windNoise(sceneWind.w * 2.0 + (float)(id % 512u) * 7.31, 6006.0) * 2.0 - 1.0;
    // The amplitude is SMALL (at most ~10 cm × weight): a sway around the neutral with a drift
    // along the flow; in a lull the cards spring back slightly against the wind.
    const float swing = (gust - 0.3) + cardSway * 0.3 * (0.3 + 0.7 * gust);
    return float3(sceneWind.x, 0.0, sceneWind.y) * (swing * 0.1 * windStrength * windWeight);
}

VSOutput main(VSInput input) {
    VSOutput output;
    float4 worldPos = mul(modelMtx, float4(input.position, 1.0));
    float gust = 0.0;
    [branch]
    if (windStrength > 0.001) {
        gust = windGustEnergy(worldPos.xz);
        worldPos.xyz += crownWindOffset(gust, input.color.a, input.id);
    }
    output.position = mul(projViewMtx, worldPos);
    output.worldPos = worldPos.xyz;
    output.worldNormal = normalize(mul((float3x3) modelMtx, input.normal));
    output.fragTexCoord = input.texCoord;
    output.fragTint = float3(1.0, 1.0, 1.0);
    output.fragLight = input.light;
    output.fragGust = gust;
    return output;
}
