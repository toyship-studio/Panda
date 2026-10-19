// Terrain: albedo — a blend of 4 layers by vertex weights (fragColor = weights from the terrain
// data), layer = color × texture (the default texture is white: it works as a pure color),
// lighting — ambient + diffuse (directional and point lights), no specular.
cbuffer MATERIAL_FIELDS {
    float4 layer1Color; // grass
    float4 layer2Color; // dirt
    float4 layer3Color; // rock
    float4 layer4Color; // sand
};

Texture2D layer1Texture;
SamplerState layer1TextureSampler;
Texture2D layer2Texture;
SamplerState layer2TextureSampler;
Texture2D layer3Texture;
SamplerState layer3TextureSampler;
Texture2D layer4Texture;
SamplerState layer4TextureSampler;

#include "panda/sun.hlsli"

// The PS input mirrors the VS output including SV_POSITION: DXBC links the stages by registers.
struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float2 fragTexCoord : TEXCOORD2;
    float4 fragColor : TEXCOORD3;
    float fragLight : TEXCOORD4;
    nointerpolation uint fragId : TEXCOORD5;
};

// Scene MRT: the second output is the emissive buffer (the terrain does not emit light).
struct PSOutput {
    float4 color : SV_Target0;
    float4 emissive : SV_Target1;
};

PSOutput main(PSInput input) {
    float4 weights = input.fragColor;
    float weightSum = max(weights.r + weights.g + weights.b + weights.a, 0.0001);
    weights /= weightSum;
    float2 uv = input.fragTexCoord;
    float3 albedo =
        layer1Color.rgb * layer1Texture.Sample(layer1TextureSampler, uv).rgb * weights.r +
        layer2Color.rgb * layer2Texture.Sample(layer2TextureSampler, uv).rgb * weights.g +
        layer3Color.rgb * layer3Texture.Sample(layer3TextureSampler, uv).rgb * weights.b +
        layer4Color.rgb * layer4Texture.Sample(layer4TextureSampler, uv).rgb * weights.a;

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
    output.color = float4(albedo * lighting * input.fragLight, 1.0);
    output.emissive = float4(0.0, 0.0, 0.0, 1.0);
    return output;
}
