// PBR metallic-roughness (Cook-Torrance GGX). Field/texture interface matches default_lit,
// so a material can switch between lit and pbr shaders without recreating its inputs.

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
    float emissiveIntensity; // HDR: glow multiplier (may be > 1)
};

#include "panda/lighting.hlsli"
#include "panda/brdf.hlsli"
#if PANDA_FEATURE_ENVIRONMENT
#include "panda/environment.hlsli" // the world environment map: sky reflections
#endif

Texture2D baseColorTexture;
SamplerState baseColorTextureSampler;
Texture2D metallicRoughnessTexture;
SamplerState metallicRoughnessTextureSampler;
Texture2D normalTexture;
SamplerState normalTextureSampler;
Texture2D occlusionTexture;
SamplerState occlusionTextureSampler;
Texture2D emissiveTexture;
SamplerState emissiveTextureSampler;

// The PS input mirrors the VS output including SV_POSITION: DXBC links the stages by registers.
struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float2 fragTexCoord : TEXCOORD2;
    float4 fragColor : TEXCOORD3;
    float fragLight : TEXCOORD4;
    nointerpolation uint fragId : TEXCOORD5;
    bool isFrontFace : SV_IsFrontFace;
};

// Scene MRT: second output — the emissive buffer (what glows and in what color; read by bloom).
struct PSOutput {
    float4 color : SV_Target0;
    float4 emissive : SV_Target1;
};

PSOutput main(PSInput input) {
    float4 baseColorSample = baseColorTexture.Sample(baseColorTextureSampler, input.fragTexCoord);
    float4 metallicRoughnessSample =
        metallicRoughnessTexture.Sample(metallicRoughnessTextureSampler, input.fragTexCoord);
    float4 normalTextureSample = normalTexture.Sample(normalTextureSampler, input.fragTexCoord);
    float3 tangentNormal = normalTextureSample.xyz * 2.0 - 1.0;
    float2 scaledNormalXY = tangentNormal.xy * normalScale;

    float3 dp1 = ddx(input.worldPos);
    float3 dp2 = ddy(input.worldPos);
    float2 duv1 = ddx(input.fragTexCoord);
    float2 duv2 = ddy(input.fragTexCoord);
    float determinant = duv1.x * duv2.y - duv1.y * duv2.x;
    float3 surfaceNormal = normalize(input.worldNormal);
    if (abs(determinant) >= 0.00001) {
        float invDeterminant = 1.0 / determinant;
        float3 tangent = normalize((dp1 * duv2.y - dp2 * duv1.y) * invDeterminant);
        float3 bitangent = normalize((dp2 * duv1.x - dp1 * duv2.x) * invDeterminant);
        float3x3 tbn = float3x3(tangent, bitangent, surfaceNormal);
        surfaceNormal = normalize(mul(float3(scaledNormalXY, tangentNormal.z), tbn));
    }

    if (doubleSided != 0u && !input.isFrontFace) {
        surfaceNormal = -surfaceNormal;
    }

    // Vertex color: only rgb tints albedo. Alpha is a data channel (vegetation wind
    // weight in COLOR_0.a) and must not leak into MASK cutout / blending.
    float4 baseColor = baseColorSample * baseColorFactor * float4(input.fragColor.rgb, 1.0);
    float alpha = baseColor.a;
    if (alphaMode == 1u && alpha < alphaCutoff) {
        discard;
    }

    // Linear pipeline: sRGB textures are decoded on sampling, constant colors
    // and lights are converted by the engine on load — the input is already linear.
    float3 albedo = baseColor.rgb;
    // The roughness the pixel needs: a normal that turns inside the pixel widens the highlight
    // instead of letting it sparkle.
    float roughness =
        filterRoughness(surfaceNormal, clamp(roughnessFactor * metallicRoughnessSample.y, 0.04, 1.0));
    float metallic = saturate(metallicFactor * metallicRoughnessSample.z);
    float occlusionSample = occlusionTexture.Sample(occlusionTextureSampler, input.fragTexCoord).x;
    float occlusion = lerp(1.0, occlusionSample, saturate(occlusionStrength));
    float3 emissiveSource =
        emissiveTexture.Sample(emissiveTextureSampler, input.fragTexCoord).xyz * emissiveFactor.xyz;
    float3 emissive = emissiveSource * emissiveIntensity;

    float3 viewDirection = normalize(cameraPosition.xyz - input.worldPos);
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    // point lights shine under a tree as well.
    float3 lighting = float3(0.0, 0.0, 0.0);
    // Occlusion volume: sky visibility dims ambient, transmittance — the sun (in sunVisibility);
    // the sample is half a cell outward along the normal — a start from the surface: the
    // object's own bark/body does not dim its own upper surfaces.
    const float2 volume = sceneOcclusion(input.worldPos + surfaceNormal * (0.5 * occlusionSurfaceOffset()));
    if (sunDirectionIntensity.w > 0.0) {
        float3 lightDirection = normalize(sunDirectionIntensity.xyz);
        float3 radiance = sunColor.rgb * sunDirectionIntensity.w *
                          sunVisibility(input.worldPos, input.worldNormal, volume.y);
        lighting += shadeLight(
            surfaceNormal, viewDirection, lightDirection, radiance, albedo / PI, metallic, roughness, f0
        );
    }

    int pointLightCount = (int)lightCounts.x;
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
        float3 radiance = pointLightColorsIntensity[i].rgb * pointLightColorsIntensity[i].w * attenuation;
        lighting += shadeLight(
            surfaceNormal, viewDirection, normalize(toLight), radiance, albedo / PI, metallic, roughness, f0
        );
    }

    // The light of the surroundings: the flat ambient of the world — a sky of one color, it
    // lights and is reflected alike — plus the environment map. Dimmed by the occlusion of the
    // material and by the sky visibility of the occlusion volume (under a canopy the sky neither
    // lights nor is reflected).
    const float3 flatAmbient = ambientColorIntensity.rgb * ambientColorIntensity.w;
    float3 irradiance = flatAmbient;
    float3 radiance = flatAmbient;
#if PANDA_FEATURE_ENVIRONMENT
    irradiance += environmentDiffuse(surfaceNormal);
    radiance += environmentSpecular(reflect(-viewDirection, surfaceNormal), roughness);
#endif
    float3 ambient = shadeAmbient(
        surfaceNormal, viewDirection, irradiance, radiance, albedo, metallic, roughness, f0,
        occlusion * ambientOcclusion(volume) * screenOcclusion(input.worldPos)
    );
    float3 color = (ambient + lighting) * input.fragLight + emissive;

    // HDR: the output is linear light; the tonemap pass at the end of the frame brings it to the display.
    const float outAlpha = (alphaMode != 2u) ? 1.0 : alpha;
    PSOutput output;
    output.color = float4(color, outAlpha);
    output.emissive = float4(emissive, outAlpha);
    return output;
}
