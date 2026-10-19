// Instanced variant of the grass vertex shader (the Lab Field preview and the mesh layers of the
// terrain flora): default_lit_instanced.vert + the same scene wind as in veg_grass.vert — see the
// comments there (three strength layers, the weight in COLOR_0.a, windStrength=0 disables the
// branch).

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

#include "panda/scene_lights.hlsli"

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
    float4 fragColor : TEXCOORD3;
    float fragLight : TEXCOORD4;
    nointerpolation uint fragId : TEXCOORD5;
    float3 rootPos : TEXCOORD6; // tuft root in world space — the CSM shadow is sampled here
};

// Copy of veg_grass.vert (see the comments there).
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

// Per-instance tone jitter (brightness ±14% + a warm-cool shift of the green): a field made of
// a single tuft does not read as clones. The formula is shared with the crown/billboard.
float3 instanceTint(float2 worldXZ) {
    // fmod keeps the sin argument small: fast-math sin gives wrong results at large coordinates
    // (the windNoise lesson — "keep the sin argument small").
    const float angle = fmod(dot(worldXZ, float2(12.9898, 78.233)), 6.2831853);
    const float h = frac(sin(angle) * 43758.5453);
    const float h2 = frac(h * 41.317 + 0.137);
    return (0.86 + 0.28 * h) * lerp(float3(1.08, 1.0, 0.86), float3(0.90, 1.0, 1.10), h2);
}

float3 grassWindOffset(float3 worldPos, float windWeight) {
    const float gust = windGustEnergy(worldPos.xz);
    const float flutterPhase = dot(worldPos.xz, float2(1.7, 2.3));
    const float flutter =
        (windNoise(sceneWind.w * 2.0 + flutterPhase, 6006.0) * 2.0 - 1.0) +
        (windNoise(sceneWind.w * 5.0 + flutterPhase * 1.7 + 11.3, 15015.0) * 2.0 - 1.0) * 0.5;
    const float lean = 0.22 * windStrength / (1.0 + windStrength);
    const float swing = lean * (0.55 + 0.9 * gust) + flutter * lean * (0.12 + 0.88 * gust) * 0.45;
    return float3(sceneWind.x, 0.0, sceneWind.y) * (swing * windWeight);
}

VSOutput main(VSInput input) {
    VSOutput output;
    // The rows of the assembled matrix = the glm columns → this is the transposed instance matrix;
    // mul(row-vector, M^T) is equivalent to M * v.
    float4x4 instMtx = float4x4(input.iModel0, input.iModel1, input.iModel2, input.iModel3);
    float4 localPos = mul(float4(input.position, 1.0), instMtx);
    float3 localNormal = mul(input.normal, (float3x3) instMtx);
    // Tuft root (instance position) in world space: the CSM shadow in the fragment shader + the
    // per-tuft dissolve.
    const float3 instanceRoot = float3(input.iModel3.x, input.iModel3.y, input.iModel3.z);
    const float3 rootWorld = mul(modelMtx, float4(instanceRoot, 1.0)).xyz;
    // Per-tuft dissolve towards the far boundary of the layer (as in Tuft): a stable threshold
    // from the world position of the root — tufts leave one by one, without a block step; each
    // one shrinks into its root over 15% of the fade zone before its threshold (no popping), the
    // remaining ones get smaller smoothly. The grassFade default is "do not fade out" (the Lab
    // preview, ordinary worlds); the distances are set by the terrain as a runtime field.
    float heightColor = input.color.a; // height fraction of the root→tip gradient
    [branch]
    if (grassFade.y > 0.001 && grassFade.y < 1.0e5) {
        const float cameraDistance = distance(rootWorld, cameraPosition.xyz);
        const float fadeT =
            saturate((cameraDistance - grassFade.x) / max(grassFade.y - grassFade.x, 0.001));
        const float threshold =
            frac(sin(dot(rootWorld.xz, float2(12.9898, 78.233))) * 43758.5453);
        const float sink = smoothstep(threshold - 0.15, threshold, fadeT);
        const float shrink = (1.0 - 0.35 * fadeT) * (1.0 - sink);
        localPos = float4(instanceRoot + (localPos.xyz - instanceRoot) * shrink, 1.0);
        // The shrink "sinks" the tuft into the ground: the upper part of the blade is what stays
        // visible, so the gradient is read by the ORIGINAL height (1 − shrink .. 1), not by the
        // compressed one — otherwise the shrink band darkens with the root color.
        heightColor = lerp(1.0 - shrink, 1.0, input.color.a);
    }
    float4 worldPos = mul(modelMtx, localPos);
    [branch]
    if (windStrength > 0.001) {
        worldPos.xyz += grassWindOffset(worldPos.xyz, input.color.a);
    }
    output.position = mul(projViewMtx, worldPos);
    output.worldPos = worldPos.xyz;
    output.worldNormal = normalize(mul((float3x3) modelMtx, localNormal));
    output.fragTexCoord = input.texCoord;
    // fragColor.rgb is the TINT CHANNEL (the grass fragment shader does not read the vertex rgb):
    // jitter per tuft by the root; a is the height fraction of the gradient, as before.
    output.fragColor = float4(instanceTint(rootWorld.xz), heightColor);
    output.fragLight = input.light;
    output.fragId = input.id;
    output.rootPos = rootWorld;
    return output;
}
