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
    // Glass (with alphaMode 2): the share of the light that goes through the surface instead of
    // being scattered by it. 0 — an ordinary surface, 1 — clear glass: only its reflections are seen.
    float transmissionFactor;
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
    // What the surface scatters: glass lets that share of the light through instead.
    const float transmission = alphaMode == 2u ? saturate(transmissionFactor) : 0.0;
    const float3 scattered = albedo * (1.0 - transmission);

    // point lights shine under a tree as well.
    float3 lighting = float3(0.0, 0.0, 0.0);
    // Occlusion volume: sky visibility dims ambient, transmittance — the sun (in sunVisibility);
    // the sample is half a cell outward along the normal — a start from the surface: the
    // object's own bark/body does not dim its own upper surfaces.
    const float2 volume = sceneOcclusion(input.worldPos + surfaceNormal * (0.5 * occlusionSurfaceOffset()));
    // The intensity of a light is what a white matte surface facing it sends back — the
    // convention of every lit shader of the engine (a sun of 1 lights such a surface to 1). In the
    // physical model that surface sends back 1 / PI of the light that arrives, so the light that
    // arrives is PI times the intensity.
    if (sunDirectionIntensity.w > 0.0) {
        float3 lightDirection = normalize(sunDirectionIntensity.xyz);
        float3 radiance = sunColor.rgb * (sunDirectionIntensity.w * PI) *
                          sunVisibility(input.worldPos, input.worldNormal, volume.y);
        lighting += shadeLight(
            surfaceNormal, viewDirection, lightDirection, radiance, scattered / PI, metallic, roughness, f0
        );
    }

    // The lamps of the world (panda/lighting.hlsli): what of each reaches the point, and from where.
    const int lampCount = localLightCount();
    [loop]
    for (int i = 0; i < lampCount; ++i) {
        float3 lightDirection;
        const float3 lightRadiance = lampLight(i, input.worldPos, input.worldNormal, lightDirection);
        float3 radiance = lightRadiance * PI;
        lighting += shadeLight(
            surfaceNormal, viewDirection, lightDirection, radiance, scattered / PI, metallic, roughness, f0
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
        surfaceNormal, viewDirection, irradiance, radiance, scattered, metallic, roughness, f0,
        occlusion * ambientOcclusion(volume) * screenOcclusion(input.worldPos)
    );
    // The light of a vertex is the share of the sky it sees (a corner between walls, a room under a
    // roof): it dims the light that comes from the sky. The sun and the lamps have their own
    // shadows and reach the surface as they are — a sunbeam through a window stays bright.
    float3 color = ambient * input.fragLight + lighting + emissive;

    // HDR: the output is linear light; the tonemap pass at the end of the frame brings it to the display.
    float outAlpha = 1.0;
    if (alphaMode == 2u) {
        // A blended surface covers what is behind it by its alpha. Glass covers less: the
        // transmitted share shows through, except what the surface reflects toward the eye — a
        // pane seen edge-on is a mirror. The blend is "color × alpha + behind × (1 − alpha)", so
        // the light of the surface itself is divided by the share that stays: a reflection on
        // clear glass arrives whole.
        const float reflectance = max(f0.r, max(f0.g, f0.b));
        const float fresnel =
            reflectance + (1.0 - reflectance) * pow(1.0 - saturate(dot(surfaceNormal, viewDirection)), 5.0);
        const float stays = max(1.0 - transmission * (1.0 - fresnel), 0.02);
        color /= stays;
        emissive /= stays;
        outAlpha = alpha * stays;
    }
    PSOutput output;
    output.color = float4(color, outAlpha);
    output.emissive = float4(emissive, outAlpha);
    return output;
}
