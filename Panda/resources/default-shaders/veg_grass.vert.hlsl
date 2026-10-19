// Vegetation grass vertex shader: default_lit.vert + scene wind. The card bends by the
// baked wind weight (COLOR_0.a: 0 root → 1 tip — the roots are pinned to the ground).
// Wind strength is THREE LAYERS (the model is shared with the veg_crown foliage and terrain grass):
//   1) global envelope — the whole scene strengthens and calms together;
//   2) local wave — a gust runs across the world along the flow (windGustEnergy = layers 1+2);
//   3) per-blade flutter — the phase drifts across the field, neighbors wave out of sync; a gust
//      boosts the amplitude (in a lull the field is almost still).
// Strength drives the ENERGY, not the lean: the mean bend saturates — the grass does not lie down.
// Strength/gustiness are system material fields (set by the engine, SceneWind); direction
// and phases come from PANDA_FIELDS. windStrength=0 disables the branch — parity with default_lit.
// MATERIAL_FIELDS is a full copy of the fragment shader layout (one std140 contract per program).

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
    float4 rootColor; // tuft gradient — fragment shader (the vertex shader does not read it)
    float4 tipColor;
    // Per-tuft dissolve towards the far boundary of the flora layer (x — start, y — end, m from the
    // camera). The template default is "do not fade out"; the terrain sets it as a runtime field.
    float4 grassFade;
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
    float4 fragColor : TEXCOORD3;
    float fragLight : TEXCOORD4;
    nointerpolation uint fragId : TEXCOORD5;
    float3 rootPos : TEXCOORD6; // tuft root in world space — the CSM shadow is sampled here
};

// 1D value noise with a PERIOD of period (in cells): random nodes, smoothstep between them.
// The phases are accumulated on the CPU with an fmod 3003 wrap (SceneWind) — periodizing the hash
// to the wave period makes the wrap seamless and keeps the sin argument small.
float windNoise(float t, float period) {
    const float f = frac(t);
    const float cell = fmod(floor(t), period);
    const float a = frac(sin(cell * 12.9898) * 43758.5453);
    const float b = frac(sin(fmod(cell + 1.0, period) * 12.9898) * 43758.5453);
    return lerp(a, b, f * f * (3.0 - 2.0 * f));
}

// Layers 1+2 of the scene wind strength at a point: the GLOBAL envelope (the whole scene, periods
// ~6/15 s) + the LOCAL wave (a gust runs along the flow, lengths 11/21 m — divisors of the
// 3003 wrap) that deflects the strength around the envelope. Gustiness sets both the depth of
// the lulls and the range of the local deviations.
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

// Horizontal displacement of a vertex by the wind (world coordinates).
float3 grassWindOffset(float3 worldPos, float windWeight) {
    const float gust = windGustEnergy(worldPos.xz);
    // Layer 3 — per-blade flutter: the phase drifts across the field (the position is unique for
    // each blade), two incommensurate octaves; a gust boosts the amplitude.
    const float flutterPhase = dot(worldPos.xz, float2(1.7, 2.3));
    const float flutter =
        (windNoise(sceneWind.w * 2.0 + flutterPhase, 6006.0) * 2.0 - 1.0) +
        (windNoise(sceneWind.w * 5.0 + flutterPhase * 1.7 + 11.3, 15015.0) * 2.0 - 1.0) * 0.5;
    // The mean bend is small and saturates — the grass leans but does not lie down.
    const float lean = 0.22 * windStrength / (1.0 + windStrength);
    const float swing = lean * (0.55 + 0.9 * gust) + flutter * lean * (0.12 + 0.88 * gust) * 0.45;
    return float3(sceneWind.x, 0.0, sceneWind.y) * (swing * windWeight);
}

VSOutput main(VSInput input) {
    VSOutput output;
    float4 worldPos = mul(modelMtx, float4(input.position, 1.0));
    [branch]
    if (windStrength > 0.001) {
        worldPos.xyz += grassWindOffset(worldPos.xyz, input.color.a);
    }
    output.position = mul(projViewMtx, worldPos);
    output.worldPos = worldPos.xyz;
    output.worldNormal = normalize(mul((float3x3) modelMtx, input.normal));
    output.fragTexCoord = input.texCoord;
    // fragColor.rgb is the per-instance tint channel (written by the instanced variant); a single
    // tuft is white, a is the height fraction of the gradient.
    output.fragColor = float4(1.0, 1.0, 1.0, input.color.a);
    output.fragLight = input.light;
    output.fragId = input.id;
    // The tuft is built from the root at the model origin.
    output.rootPos = mul(modelMtx, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    return output;
}
