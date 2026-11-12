// The lighting of a lit material shader: the camera (view) and the lights, plus the sun shadows
// and the Occlusion Volume when the project has those features (context/shader-features.md). The
// helpers degrade without a feature — a shader written against this include compiles in any
// project: without `lights` the scene is unlit (a white ambient of 1, no sun, no point lights —
// the shader outputs its albedo), without `shadows` the sun is open, without `occlusion` the sky
// and the sun are open and the sample offset is zero. Vegetation declares
// PANDA_SHADOW_SINGLE_TAP before the include (see panda/shadows.hlsli).
#include "panda/view.hlsli"
#if PANDA_FEATURE_LIGHTS
#include "panda/lights.hlsli"
#else
static const float4 ambientColorIntensity = float4(1.0, 1.0, 1.0, 1.0);
static const float4 sunDirectionIntensity = float4(0.0, 1.0, 0.0, 0.0);
static const float4 sunColor = float4(1.0, 1.0, 1.0, 1.0);
static const uint4 lightCounts = uint4(0u, 0u, 0u, 0u);
static const float4 pointLightPositionsRange[8] = {
    float4(0.0, 0.0, 0.0, 0.0), float4(0.0, 0.0, 0.0, 0.0), float4(0.0, 0.0, 0.0, 0.0), float4(0.0, 0.0, 0.0, 0.0),
    float4(0.0, 0.0, 0.0, 0.0), float4(0.0, 0.0, 0.0, 0.0), float4(0.0, 0.0, 0.0, 0.0), float4(0.0, 0.0, 0.0, 0.0)
};
static const float4 pointLightColorsIntensity[8] = {
    float4(0.0, 0.0, 0.0, 0.0), float4(0.0, 0.0, 0.0, 0.0), float4(0.0, 0.0, 0.0, 0.0), float4(0.0, 0.0, 0.0, 0.0),
    float4(0.0, 0.0, 0.0, 0.0), float4(0.0, 0.0, 0.0, 0.0), float4(0.0, 0.0, 0.0, 0.0), float4(0.0, 0.0, 0.0, 0.0)
};
#endif
#if PANDA_FEATURE_SHADOWS
#include "panda/shadows.hlsli"
#endif
#if PANDA_FEATURE_OCCLUSION
#include "panda/occlusion.hlsli"
#endif

// Sky visibility (x) and sun transmittance (y) of the Occlusion Volume at a point; (1, 1) without
// the feature. Sample outward from the surface by occlusionSurfaceOffset() along the normal: the
// object's own body must not dim its upper surfaces.
float2 sceneOcclusion(float3 worldPos) {
#if PANDA_FEATURE_OCCLUSION
    return volumeOcclusion(worldPos);
#else
    return float2(1.0, 1.0);
#endif
}

// The sample offset from the surface (m) of the Occlusion Volume; 0 without the feature.
float occlusionSurfaceOffset() {
#if PANDA_FEATURE_OCCLUSION
    return occlusionTexel.w;
#else
    return 0.0;
#endif
}

// The ambient factor of the volume at a point: the sky visibility weighted by the volume's
// ambient strength; 1 without the feature.
float ambientOcclusion(float2 occlusion) {
#if PANDA_FEATURE_OCCLUSION
    return lerp(1.0, occlusion.x, occlusionParams.x);
#else
    return 1.0;
#endif
}

// The cascade shadow 0..1 of the sun at a point (1 — lit) and the CSM share csmWeight (1 in the
// shadow zone, → 0 at Shadow Distance); 1 and 0 without the feature.
float sunShadow(float3 worldPos, float3 worldNormal, out float csmWeight) {
#if PANDA_FEATURE_SHADOWS
    return sampleShadow(worldPos, worldNormal, csmWeight);
#else
    csmWeight = 0.0;
    return 1.0;
#endif
}

// The shadow strength of the world (0 — no shadows this frame); 0 without the feature.
float shadowStrength() {
#if PANDA_FEATURE_SHADOWS
    return shadowParams.w;
#else
    return 0.0;
#endif
}

// The sun factor of the canopy transmittance (y of sceneOcclusion) at a point, weighted by the
// CSM share: small inside the CSM distance, full beyond it; 1 without the feature.
float sunTransmittance(float volumeTransmittance, float csmWeight) {
#if PANDA_FEATURE_OCCLUSION
    return lerp(1.0, volumeTransmittance, volumeSunStrength(csmWeight));
#else
    return 1.0;
#endif
}

// Sun visibility at a point — both techniques at once: cascade shadows up close, beyond Shadow
// Distance their role is taken over by the Occlusion Volume transmittance (y of sceneOcclusion at
// the receiver point).
float sunVisibility(float3 worldPos, float3 worldNormal, float volumeTransmittance) {
    float csmWeight;
    float shadow = sunShadow(worldPos, worldNormal, csmWeight);
    return lerp(1.0, shadow, csmWeight) * sunTransmittance(volumeTransmittance, csmWeight);
}
