// Sun visibility at a point — both techniques at once: cascade shadows up close (shadow.hlsli),
// beyond Shadow Distance their role is taken over by the occlusion volume transmittance
// (occlusion.hlsli). volumeTransmittance is y from volumeOcclusion at the receiver point.
#include "panda/shadow.hlsli"
#include "panda/occlusion.hlsli"

float sunVisibility(float3 worldPos, float3 worldNormal, float volumeTransmittance) {
    float csmWeight;
    float shadow = sampleShadow(worldPos, worldNormal, csmWeight);
    return lerp(1.0, shadow, csmWeight) * lerp(1.0, volumeTransmittance, volumeSunStrength(csmWeight));
}
