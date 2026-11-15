// PANDA_ENVIRONMENT — the environment map of the world (shader feature `environment`) and the
// light a surface takes from it. The block is generated from Panda::EnvironmentUbo; with the
// feature off the include is a compile error.
//
// System textures:
//   environmentMap      — the map itself (equirect: the top of the image is the zenith,
//                         u = atan2(z, x); without a map of the world — the built-in sky gradient);
//   environmentLighting — the light of the map, baked by the engine (Panda::EnvironmentLighting):
//                         an atlas of equirect tiles, environmentTiles[0..4] — the radiance
//                         prefiltered with the GGX lobe of roughness 0.2, 0.4 … 1,
//                         environmentTiles[5] — the irradiance over pi.
// environmentParams: x — exposure of the map (the sky and its light together), y — the highest mip
// of the map, z — 1 when the world has a map, else 0: the functions below then return black.
//
// A lit shader does not read the atlas itself:
//   environmentDiffuse(normal)               — the light of the sky on a matte surface; multiply
//                                              by the diffuse albedo;
//   environmentSpecular(reflected, roughness) — the light of the sky a surface of this roughness
//                                              reflects along `reflected`; multiply by the
//                                              reflectance (panda/brdf.hlsli, environmentBrdf).
#include "panda/blocks/environment.hlsli"

// The place of a world direction in an equirectangular map.
float2 environmentUv(float3 direction) {
    return float2(atan2(direction.z, direction.x), asin(clamp(-direction.y, -1.0, 1.0))) * float2(0.1591549, 0.3183099) + 0.5;
}

float3 environmentTile(int tile, float2 uv) {
    const float4 rect = environmentTiles[tile];
    return environmentLighting.SampleLevel(environmentLightingSampler, rect.xy + uv * rect.zw, 0.0).rgb;
}

// The light of the environment on a matte surface with this normal (the irradiance over pi, with
// the exposure of the map): multiply by the diffuse albedo. Black without a map.
float3 environmentDiffuse(float3 normal) {
    return environmentTile(5, environmentUv(normal)) * environmentParams.x;
}

// The light of the environment a surface of this roughness reflects along `reflected` (the
// radiance of the map prefiltered with the GGX lobe, with the exposure of the map): multiply by
// the reflectance of the surface. Black without a map.
float3 environmentSpecular(float3 reflected, float roughness) {
    const float2 uv = environmentUv(reflected);
    // The mip of the map a mirror needs: by the angle a pixel covers (the derivatives of uv break
    // at the seam of atan2).
    const float radiansPerPixel = max(length(ddx(reflected)), length(ddy(reflected)));
    // The levels are 0.2 of roughness apart: 0 — the map itself, 1..5 — the tiles.
    const float level = saturate(roughness) * 5.0;
    float3 radiance;
    [branch]
    if (level < 1.0) {
        float2 mapSize;
        environmentMap.GetDimensions(mapSize.x, mapSize.y);
        // The first tile is 256 texels wide: the map reaches the same blur at this mip.
        const float tileMip = max(log2(mapSize.x / 256.0), 0.0);
        const float pixelMip = log2(max(radiansPerPixel * mapSize.x * 0.1591549, 1.0e-6));
        const float3 sharp =
            environmentMap.SampleLevel(environmentMapSampler, uv, max(pixelMip, level * tileMip)).rgb *
            environmentParams.z;
        radiance = lerp(sharp, environmentTile(0, uv), level * level);
    } else {
        const int lower = min(int(level) - 1, 4);
        const int upper = min(lower + 1, 4);
        radiance = lerp(environmentTile(lower, uv), environmentTile(upper, uv), frac(level));
    }
    return radiance * environmentParams.x;
}
