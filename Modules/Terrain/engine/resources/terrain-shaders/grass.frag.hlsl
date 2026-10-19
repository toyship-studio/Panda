cbuffer MATERIAL_FIELDS {
    float4 colorRoot;
    float4 colorTip;
    float4 colorExtra;
    float4 windParams;
    float4 timeFade;
    float4 fadeCamera;
    uint doubleSided;
};

#define PANDA_SHADOW_SINGLE_TAP // vegetation: a single compare tap (see shadow.hlsli)
#include "panda/sun.hlsli"

// Tiling fbm: G — large-scale color patches of the field (moisture/soil).
Texture2D noiseTexture;
SamplerState noiseTextureSampler;

static const float PATCH_SCALE = 90.0; // meters per period of the color patches

struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float3 hTint : TEXCOORD2; // x — vertex h, y — tuft tint, z — blade random
    float3 rootPos : TEXCOORD3; // tuft root in world space
};

// Scene MRT: the second output is the emissive buffer (grass does not emit light).
struct PSOutput {
    float4 color : SV_Target0;
    float4 emissive : SV_Target1;
};

PSOutput main(PSInput input) {
    const float h = input.hTint.x;
    // Root → tip gradient + a slight per-tuft tone variation (±12%).
    float3 albedo = lerp(colorRoot.rgb, colorTip.rgb, h) * (0.88 + 0.24 * input.hTint.y);
    // Random "dry" blades: ~18% of the blades are tinted with the third color (by a stable
    // blade×tuft random); the gradient towards the root is kept — the stem stays dark.
    const float extraMask = step(0.82, input.hTint.z);
    albedo = lerp(albedo, colorExtra.rgb * (0.45 + 0.55 * h), extraMask * 0.85);
    // Large patches of the field (moisture/soil): smooth brightness modulation over world XZ.
    const float patch = noiseTexture.Sample(noiseTextureSampler, input.worldPos.xz / PATCH_SCALE).g;
    albedo *= 0.84 + 0.32 * patch;

    float3 surfaceNormal = normalize(input.worldNormal);
    // point lights shine under a tree as well.
    // Occlusion volume: grass under the canopy gets darker (sampled at the tuft root).
    const float2 volume = volumeOcclusion(input.rootPos);
    float3 lighting = ambientColorIntensity.rgb * ambientColorIntensity.w * lerp(1.0, volume.x, occlusionParams.x);

    if (directionalDirectionIntensity.w > 0.0) {
        float3 lightDirection = normalize(directionalDirectionIntensity.xyz);
        // Half-Lambert: grass is thin double-sided petals, hard Lambert yields
        // black turned-away blades.
        float diffuse = saturate(dot(surfaceNormal, lightDirection) * 0.5 + 0.5);
        // CSM shadow — at the tuft ROOT (a blade swaying in the wind would pass through shadow
        // edges and flicker); normal offset — upward (the blade is thin, its normal faces
        // sideways; the acne comes from the ground under the grass).
        lighting += directionalColor.rgb * directionalDirectionIntensity.w * diffuse *
                    sunVisibility(input.rootPos, float3(0.0, 1.0, 0.0), volume.y);
    }

    int pointLightCount = (int)sceneLightCounts.x;
    [loop]
    for (int i = 0; i < pointLightCount; ++i) {
        float3 toLight = pointLightPositionsRange[i].xyz - input.worldPos;
        float distanceToLight = length(toLight);
        float radius = pointLightPositionsRange[i].w;
        if (radius <= 0.0 || distanceToLight >= radius) {
            continue;
        }
        float normalizedDistance = saturate(distanceToLight / radius);
        float attenuation = 1.0 - normalizedDistance * normalizedDistance;
        attenuation *= attenuation;
        float diffuse = saturate(dot(surfaceNormal, normalize(toLight)) * 0.5 + 0.5);
        lighting += pointLightColorsIntensity[i].rgb * pointLightColorsIntensity[i].w * attenuation *
                    diffuse;
    }

    // Darkening towards the root ties the grass to the ground (a cheap root AO).
    lighting *= 0.55 + 0.45 * h;

    PSOutput output;
    output.color = float4(albedo * lighting, 1.0);
    output.emissive = float4(0.0, 0.0, 0.0, 1.0);
    return output;
}
