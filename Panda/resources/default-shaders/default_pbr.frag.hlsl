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

#include "panda/sun.hlsli"
#include "panda/brdf.hlsli"

// World environment map (system input, equirect: the top of the image — zenith, u = atan2(z, x)).
Texture2D environmentMap;
SamplerState environmentMapSampler;

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

// Environment map reflection: a sample along the reflected ray, blur by roughness — through
// the map's mip (roughness × highest mip; SampleLevel bypasses the atan2 discontinuity at the
// seam), the weight — the split-sum approximation of the environment BRDF without a LUT (Karis,
// "Physically Based Shading on Mobile"): F0 · A + B from N·V and roughness — a dielectric
// reflects at grazing angles, a metal — with the albedo color.
float3 environmentReflection(float3 normal, float3 viewDirection, float3 f0, float roughness) {
    const float3 reflected = reflect(-viewDirection, normal);
    const float2 uv =
        float2(atan2(reflected.z, reflected.x), asin(clamp(-reflected.y, -1.0, 1.0))) * float2(0.1591, 0.3183) + 0.5;
    const float3 radiance =
        environmentMap.SampleLevel(environmentMapSampler, uv, roughness * environmentParams.y).rgb *
        environmentParams.x;
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    const float4 r = roughness * c0 + c1;
    const float nDotV = saturate(dot(normal, viewDirection));
    const float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    const float2 scaleBias = float2(-1.04, 1.04) * a004 + r.zw;
    return radiance * (f0 * scaleBias.x + scaleBias.y);
}

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
    float roughness = clamp(roughnessFactor * metallicRoughnessSample.y, 0.04, 1.0);
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
    const float2 volume = volumeOcclusion(input.worldPos + surfaceNormal * (0.5 * occlusionTexel.w));
    if (directionalDirectionIntensity.w > 0.0) {
        float3 lightDirection = normalize(directionalDirectionIntensity.xyz);
        float3 radiance = directionalColor.rgb * directionalDirectionIntensity.w *
                          sunVisibility(input.worldPos, input.worldNormal, volume.y);
        lighting += shadeLight(
            surfaceNormal, viewDirection, lightDirection, radiance, albedo / PI, metallic, roughness, f0
        );
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
        float3 radiance = pointLightColorsIntensity[i].rgb * pointLightColorsIntensity[i].w * attenuation;
        lighting += shadeLight(
            surfaceNormal, viewDirection, normalize(toLight), radiance, albedo / PI, metallic, roughness, f0
        );
    }

    float3 ambient = ambientColorIntensity.rgb * ambientColorIntensity.w * albedo * occlusion *
                     lerp(1.0, volume.x, occlusionParams.x);
    // Sky reflections: dimmed by the material AO and by the sky visibility from the occlusion
    // volume (under the canopy the sky is not reflected), like ambient.
    [branch]
    if (environmentParams.z > 0.5) {
        ambient += environmentReflection(surfaceNormal, viewDirection, f0, roughness) * occlusion *
                   lerp(1.0, volume.x, occlusionParams.x);
    }
    float3 color = (ambient + lighting) * input.fragLight + emissive;

    // HDR: the output is linear light; the implicit Tonemap pass at frame end does Reinhard + sRGB.
    const float outAlpha = (alphaMode != 2u) ? 1.0 : alpha;
    PSOutput output;
    output.color = float4(color, outAlpha);
    output.emissive = float4(emissive, outAlpha);
    return output;
}
