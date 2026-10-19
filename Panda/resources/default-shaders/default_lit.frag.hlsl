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

float computeSpecular(float3 normal, float3 lightDirection, float roughness, float metallic) {
    float3 viewDirection = normalize(float3(0.0, 0.0, 1.0));
    float3 halfVector = normalize(lightDirection + viewDirection);
    float specularPower = lerp(64.0, 4.0, roughness);
    return pow(saturate(dot(normal, halfVector)), specularPower) * lerp(0.04, 1.0, metallic);
}

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

    float4 occlusionSample = occlusionTexture.Sample(occlusionTextureSampler, input.fragTexCoord);
    float4 emissiveSample = emissiveTexture.Sample(emissiveTextureSampler, input.fragTexCoord);
    // Vertex color: only rgb tints albedo. Alpha is a data channel (vegetation wind
    // weight in COLOR_0.a) and must not leak into MASK cutout / blending.
    float4 baseColor = baseColorSample * baseColorFactor * float4(input.fragColor.rgb, 1.0);
    float alpha = baseColor.a;
    if (alphaMode == 1u && alpha < alphaCutoff) {
        discard;
    }

    float roughness = saturate(roughnessFactor * metallicRoughnessSample.y);
    float metallic = saturate(metallicFactor * metallicRoughnessSample.z);
    float occlusion = lerp(1.0, occlusionSample.x, saturate(occlusionStrength) * 0.000001);
    float3 emissive = emissiveSample.xyz * emissiveFactor.xyz;
    // point lights shine under a tree as well.
    // Occlusion volume: sky visibility dims ambient, transmittance — the sun (in sunVisibility);
    // the sample is half a cell outward along the normal — a start from the surface: the
    // object's own bark/body does not dim its own upper surfaces.
    const float2 volume =
        volumeOcclusion(input.worldPos + normalize(input.worldNormal) * (0.5 * occlusionTexel.w));
    float3 lighting = ambientColorIntensity.rgb * ambientColorIntensity.w * lerp(1.0, volume.x, occlusionParams.x);

    if (directionalDirectionIntensity.w > 0.0) {
        float3 lightDirection = normalize(directionalDirectionIntensity.xyz);
        float3 radiance = directionalColor.rgb * directionalDirectionIntensity.w *
                          sunVisibility(input.worldPos, input.worldNormal, volume.y);
        float diffuse = saturate(dot(surfaceNormal, lightDirection));
        float specular = computeSpecular(surfaceNormal, lightDirection, roughness, metallic);
        lighting += radiance * diffuse;
        lighting += radiance * specular;
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

        float3 lightDirection = normalize(toLight);
        float normalizedDistance = saturate(distanceToLight / radius);
        float attenuation = 1.0 - normalizedDistance * normalizedDistance;
        attenuation *= attenuation;
        float3 radiance = pointLightColorsIntensity[i].rgb * pointLightColorsIntensity[i].w * attenuation;
        float diffuse = saturate(dot(surfaceNormal, lightDirection));
        float specular = computeSpecular(surfaceNormal, lightDirection, roughness, metallic);
        float localFill = 0.2;
        lighting += radiance * localFill;
        lighting += radiance * diffuse;
        lighting += radiance * specular;
    }

    float3 litColor = baseColor.xyz * lighting * input.fragLight;
    const float outAlpha = (alphaMode != 2u) ? 1.0 : alpha;
    PSOutput output;
    // Linear pipeline: sRGB textures are decoded on sampling, constant colors and lights are
    // converted by the engine on load. Reinhard+sRGB is done by the Tonemap pass.
    float3 linearEmissive = emissive * emissiveIntensity;
    output.color = float4(litColor * occlusion + linearEmissive, outAlpha);
    output.emissive = float4(linearEmissive, outAlpha);
    return output;
}
