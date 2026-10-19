// Heightmap terrain, "details at the feet — a color mass in the distance": up close a layer is
// the pure texture (the layer colors take no part in near at all), in the grass fade zone it
// crossfades into the layer colors. On layer 1 (Grass) the far color is the mosaic of the terrain
// grass palette as is (the layer has no color of its own): the disappearing grass changes
// neither the tone nor the texture of the field.
// Lighting — ambient + diffuse (directional and point lights), no specular.
cbuffer MATERIAL_FIELDS {
    float4 layer2Color; // dirt
    float4 layer3Color; // rock
    float4 layer4Color; // sand
    // Far view — a runtime material field, fed by Terrain2DAsset every frame.
    float4 farFade; // x — crossfade start (m), y — end, z — grain mosaic tile (m)
};

Texture2D layer1Texture;
SamplerState layer1TextureSampler;
Texture2D layer2Texture;
SamplerState layer2TextureSampler;
Texture2D layer3Texture;
SamplerState layer3TextureSampler;
Texture2D layer4Texture;
SamplerState layer4TextureSampler;
// Terrain asset splatmap (layer weights over the map) — a runtime slot, fed by Terrain2DAsset.
Texture2D splatmap;
SamplerState splatmapSampler;
// Baked mosaic of grass grains (TerrainGrainTexture): an opaque tile of the grains and the soil
// of the palette; the mips give exact anti-aliasing of the distance. A runtime slot; until the
// mosaic is baked, the fade stays beyond the horizon and the slot is not read.
Texture2D farGrainTexture;
SamplerState farGrainTextureSampler;

#include "panda/sun.hlsli"

// The PS input mirrors the VS output including SV_POSITION: DXBC links the stages by registers.
struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float2 tileUV : TEXCOORD2;
    float2 splatUV : TEXCOORD3;
};

// Scene MRT: the second output is the emissive buffer (the terrain does not emit light).
struct PSOutput {
    float4 color : SV_Target0;
    float4 emissive : SV_Target1;
};

PSOutput main(PSInput input) {
    float4 weights = splatmap.Sample(splatmapSampler, input.splatUV);
    float weightSum = max(weights.r + weights.g + weights.b + weights.a, 0.0001);
    weights /= weightSum;
    float2 uv = input.tileUV;

    // Up close: layer = pure texture (the layer colors take no part here). The samples are
    // unconditional (mip gradients), the contribution is faded out by the crossfade.
    float3 nearColor = layer1Texture.Sample(layer1TextureSampler, uv).rgb * weights.r +
                       layer2Texture.Sample(layer2TextureSampler, uv).rgb * weights.g +
                       layer3Texture.Sample(layer3TextureSampler, uv).rgb * weights.b +
                       layer4Texture.Sample(layer4TextureSampler, uv).rgb * weights.a;

    // In the distance: layer 1 — the grain mosaic of the grass palette as is (a texel = a grain or
    // the soil of the palette, the mip chain averages the pattern into a color mass), layers
    // 2..4 — their colors. No noise, no darkening: the distance is colored only by the palette
    // and the scene lighting.
    float2 grainUV = input.worldPos.xz / farFade.z;
    float3 grassGrain = farGrainTexture.Sample(farGrainTextureSampler, grainUV).rgb;
    float3 farColor = grassGrain * weights.r + layer2Color.rgb * weights.g +
                      layer3Color.rgb * weights.b + layer4Color.rgb * weights.a;

    float cameraDistance = distance(input.worldPos, cameraPosition.xyz);
    float farT = smoothstep(farFade.x, farFade.y, cameraDistance);
    float3 albedo = lerp(nearColor, farColor, farT);

    float3 surfaceNormal = normalize(input.worldNormal);
    // point lights shine under a tree as well.
    // Occlusion volume: the ground under the canopy gets darker (sky visibility), the sun —
    // transmittance.
    const float2 volume = volumeOcclusion(input.worldPos);
    float3 lighting = ambientColorIntensity.rgb * ambientColorIntensity.w * lerp(1.0, volume.x, occlusionParams.x);

    if (directionalDirectionIntensity.w > 0.0) {
        float3 lightDirection = normalize(directionalDirectionIntensity.xyz);
        float diffuse = saturate(dot(surfaceNormal, lightDirection));
        lighting += directionalColor.rgb * directionalDirectionIntensity.w * diffuse *
                    sunVisibility(input.worldPos, input.worldNormal, volume.y);
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
        float diffuse = saturate(dot(surfaceNormal, normalize(toLight)));
        lighting += pointLightColorsIntensity[i].rgb * pointLightColorsIntensity[i].w * attenuation *
                    (0.2 + diffuse);
    }

    PSOutput output;
    output.color = float4(albedo * lighting, 1.0);
    output.emissive = float4(0.0, 0.0, 0.0, 1.0);
    return output;
}
