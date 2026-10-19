// Occlusion volume (OV, context/lighting.md): a chunk grid anchored to the world, a
// resident window in a single RGBA8 3D texture (R — sky visibility, attenuates ambient;
// G and B — two sun transmittance buffers: a sun rebake is published into the free channel and
// blended in with the weight occlusionFocus.w; inside the CSM distance the transmittance has a
// small weight, beyond it — a full one). The parameters are the tail of SCENE_LIGHTS (occlusion*).
#include "panda/scene_lights.hlsli"

// Volume of the resident window chunks (a system light input, like shadowMap).
Texture3D occlusionVolume;
SamplerState occlusionVolumeSampler;

// Sky visibility (x) and sun transmittance (y) at a point: the chunk by position (the grid is
// anchored to the world), the slot is the chunk index modulo the window size, the slot tile with a
// one-cell border — a hardware trilinear sample is seamless at chunk joins; slices are counted
// from the bottom of the chunk volume. Outside the window, in an unbaked slot and without a
// volume — open. A ready chunk fades in with the slot weight (occlusionChunks[i].w — a CPU ramp);
// at the window edge the occlusion fades out by distance from the FOCUS (occlusionFocus.xy), not
// from the edge: the window moves in chunk steps, the falloff is continuous with the camera; the
// falloff width is 16 cells up to occlusionFocus.z (the guaranteed residency radius).
float2 volumeOcclusion(float3 worldPos) {
    if (occlusionWindow.w <= 0.0) { return float2(1.0, 1.0); }
    const float cell = max(occlusionGrid.x, 0.001);
    const float chunkWorld = occlusionGrid.y * cell;
    const float window = occlusionWindow.z;
    const float2 chunk = floor(worldPos.xz / chunkWorld);
    const float2 inWindow = chunk - occlusionWindow.xy;
    if (any(inWindow < 0.0) || any(inWindow >= window)) { return float2(1.0, 1.0); }
    const float2 slot = chunk - window * floor(chunk / window);
    const float4 entry = occlusionChunks[int(slot.y) * int(window) + int(slot.x)];
    if (entry.w <= 0.0 || entry.x != chunk.x || entry.y != chunk.y) { return float2(1.0, 1.0); }
    const float2 texelXZ = (worldPos.xz - chunk * chunkWorld) / cell + 1.0 + slot * occlusionGrid.z;
    const float slice = clamp((worldPos.y - entry.z) / cell, 0.5, occlusionGrid.w - 0.5);
    const float3 volume =
        occlusionVolume.SampleLevel(occlusionVolumeSampler, float3(texelXZ.x, slice, texelXZ.y) * occlusionTexel.xyz, 0.0).xyz;
    // Contrast: a power of the sky visibility — the interior darkens, the open stays as is.
    float2 occlusion = float2(pow(volume.x, max(occlusionParams.w, 0.01)), lerp(volume.y, volume.z, occlusionFocus.w));
    const float edge = saturate((occlusionFocus.z - distance(worldPos.xz, occlusionFocus.xy)) / (16.0 * cell));
    return lerp(float2(1.0, 1.0), occlusion, edge * entry.w);
}

// Weight of the canopy transmittance by the CSM share at a point: small inside the CSM distance
// (CSM holds the sharp shadow of the crowns), full beyond it (the only light and shade of the
// distant forest).
float volumeSunStrength(float csmWeight) {
    return lerp(occlusionParams.z, occlusionParams.y, csmWeight);
}
