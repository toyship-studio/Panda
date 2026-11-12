// The default sky draws the world's ENVIRONMENT MAP — the system texture environmentMap
// (Settings → World → Environment Map; without it the engine puts a built-in gradient into the
// slot): equirect, the top of the image is the zenith, u = atan2(z, x). Exposure is
// environmentParams.x of the environment feature (panda/environment.hlsli).
// There is deliberately NO sun here: the disk is a property of the world, it lives in the
// project's custom sky material (sky_gradient in Neverland / TerrainTest). A project without the
// environment feature gets the built-in gradient computed here (the same colors as the
// placeholder texture): the sky never goes black because a feature is off.
#if PANDA_FEATURE_ENVIRONMENT
#include "panda/environment.hlsli"
#endif

// The PS input mirrors the VS output including SV_POSITION: DXBC links the stages by registers.
struct PSInput {
    float4 position : SV_POSITION;
    float3 texCoord : TEXCOORD0;
};

// Scene MRT: the second output is the emissive buffer (glow contribution; there is none here).
struct PSOutput {
    float4 color : SV_Target0;
    float4 emissive : SV_Target1;
};

static const float2 invAtan = float2(0.1591, 0.3183);

PSOutput main(PSInput input) {
    const float3 direction = normalize(input.texCoord);
#if PANDA_FEATURE_ENVIRONMENT
    const float2 uv = float2(atan2(direction.z, direction.x), asin(clamp(-direction.y, -1.0, 1.0))) * invAtan + 0.5;
    // The mip is derived from the angle per pixel, not from the uv derivatives: atan2 has a
    // discontinuity at the ±π seam, and the hardware mip selection would draw a line there.
    float2 mapSize;
    environmentMap.GetDimensions(mapSize.x, mapSize.y);
    const float radiansPerPixel = max(length(ddx(direction)), length(ddy(direction)));
    const float lod = max(log2(max(radiansPerPixel * mapSize.x * 0.15915, 1.0e-6)), 0.0);
    // The sample is linear (an sRGB texture is decoded by the hardware, .hdr is already linear
    // light): into the HDR buffer as is, the final pass does the tonemapping.
    const float3 color = environmentMap.SampleLevel(environmentMapSampler, uv, lod).rgb * environmentParams.x;
#else
    // The built-in gradient (StaticResources::defaultSkyTexture, authored colors in sRGB): zenith →
    // horizon → ground by the elevation of the direction, in linear light.
    const float3 zenith = pow(float3(0.20, 0.42, 0.78), 2.2);
    const float3 horizon = pow(float3(0.74, 0.83, 0.92), 2.2);
    const float3 ground = pow(float3(0.34, 0.32, 0.30), 2.2);
    const float t = saturate(abs(direction.y));
    const float3 color = direction.y >= 0.0 ? lerp(horizon, zenith, sqrt(t)) : lerp(horizon * 0.8, ground, sqrt(t));
#endif

    PSOutput output;
    output.color = float4(color, 1.0);
    output.emissive = float4(0.0, 0.0, 0.0, 0.0);
    return output;
}
