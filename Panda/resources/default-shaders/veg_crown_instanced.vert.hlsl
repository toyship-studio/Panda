// Instanced variant of the crown vertex shader (terrain flora draws trees as instances):
// default_lit_instanced.vert + the same scene wind as in veg_crown.vert — see the comments
// there (the energy is shared with the frag, the weight is in COLOR_0.a, the card is displaced
// as a whole, windStrength = 0 disables the branch).

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
    // Instance data (second vertex stream, per-instance step): the columns of the instance
    // transform matrix (glm column-major). The locations continue the vertex ones.
    float4 iModel0 : TEXCOORD3;
    float4 iModel1 : TEXCOORD4;
    float4 iModel2 : TEXCOORD5;
    float4 iModel3 : TEXCOORD6;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float2 fragTexCoord : TEXCOORD2;
    float fragLight : TEXCOORD4;
    // Per-instance tone jitter (the vertex color is no longer a color but parameters —
    // the tint rides in a separate varying, the frag multiplies the albedo).
    nointerpolation float3 fragTint : TEXCOORD6;
    // Gust energy (wind layers 1+2) — smooth over the world, computed here once
    // per card, not per pixel (the frag keeps only the per-leaf flutter for itself).
    float fragGust : TEXCOORD7;
};

// A copy of veg_crown.vert (see the comments there).
float windNoise(float t, float period) {
    const float f = frac(t);
    const float cell = fmod(floor(t), period);
    const float a = frac(sin(cell * 12.9898) * 43758.5453);
    const float b = frac(sin(fmod(cell + 1.0, period) * 12.9898) * 43758.5453);
    return lerp(a, b, f * f * (3.0 - 2.0 * f));
}

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

// Per-instance tone jitter (brightness ±14% + a warm-cold shift of the green): a forest of a
// single species does not read as clones. The hash is the world XZ position of the plant base,
// the same formula is in the impostor billboard — the tone does not jump at the zone transition.
float3 instanceTint(float2 worldXZ) {
    // fmod keeps the sin argument small: fast-math sin lies at large coordinates
    // (the windNoise lesson — "keep the sin argument small").
    const float angle = fmod(dot(worldXZ, float2(12.9898, 78.233)), 6.2831853);
    const float h = frac(sin(angle) * 43758.5453);
    const float h2 = frac(h * 41.317 + 0.137);
    return (0.86 + 0.28 * h) * lerp(float3(1.08, 1.0, 0.86), float3(0.90, 1.0, 1.10), h2);
}

float3 crownWindOffset(float gust, float windWeight, uint id) {
    const float cardSway =
        windNoise(sceneWind.w * 2.0 + (float)(id % 512u) * 7.31, 6006.0) * 2.0 - 1.0;
    const float swing = (gust - 0.3) + cardSway * 0.3 * (0.3 + 0.7 * gust);
    return float3(sceneWind.x, 0.0, sceneWind.y) * (swing * 0.1 * windStrength * windWeight);
}

VSOutput main(VSInput input) {
    VSOutput output;
    // The rows of the assembled matrix = the glm columns → this is the transposed instance matrix;
    // mul(row-vector, M^T) is equivalent to M * v.
    float4x4 instMtx = float4x4(input.iModel0, input.iModel1, input.iModel2, input.iModel3);
    float4 localPos = mul(float4(input.position, 1.0), instMtx);
    float3 localNormal = mul(input.normal, (float3x3) instMtx);
    float4 worldPos = mul(modelMtx, localPos);
    float gust = 0.0;
    [branch]
    if (windStrength > 0.001) {
        gust = windGustEnergy(worldPos.xz);
        worldPos.xyz += crownWindOffset(gust, input.color.a, input.id);
    }
    output.position = mul(projViewMtx, worldPos);
    output.worldPos = worldPos.xyz;
    output.worldNormal = normalize(mul((float3x3) modelMtx, localNormal));
    output.fragTexCoord = input.texCoord;
    output.fragTint = instanceTint(mul(modelMtx, float4(input.iModel3.xyz, 1.0)).xz);
    output.fragLight = input.light;
    output.fragGust = gust;
    return output;
}
