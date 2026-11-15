// PANDA_SSAO — the screen-space ambient occlusion (shader feature `ssao`, Panda::SsaoRenderer):
// how much of its surroundings a point sees past the geometry next to it, the contact darkening
// of the ambient light. The system texture ssaoMap holds it for the frame that was drawn last
// (r — the visible share, g — the view depth of the point, m); ssaoViewProj is the view-projection
// of that frame, ssaoParams.x — the intensity (0 — no occlusion), ssaoParams.y — 1 when the rows of
// the map go bottom-up. The block is generated from Panda::SsaoUbo; with the feature off the
// include is a compile error — a lit shader takes panda/lighting.hlsli, whose screenOcclusion
// degrades to "open" without it.
#include "panda/blocks/ssao.hlsli"

// The share of its surroundings a point of the world sees (1 — open): multiply the ambient light
// by it, not the light of the sun or of a lamp. The point is looked up where it was on screen in
// the last frame; a point that frame did not show (it was hidden or off screen, or it has moved)
// is recognized by its depth and is open until the next frame.
float sampleScreenOcclusion(float3 worldPos) {
    if (ssaoParams.x <= 0.0) { return 1.0; }
    const float4 clip = mul(ssaoViewProj, float4(worldPos, 1.0));
    if (clip.w <= 0.0) { return 1.0; }
    const float2 ndc = clip.xy / clip.w;
    if (any(abs(ndc) > 1.0)) { return 1.0; }
    float2 uv = ndc * 0.5 + 0.5;
    if (ssaoParams.y < 0.5) { uv.y = 1.0 - uv.y; }
    const float2 occlusion = ssaoMap.SampleLevel(ssaoMapSampler, uv, 0.0).rg;
    // clip.w of a perspective projection is the view depth. The map is half the resolution of the
    // frame and filtered: on a surface seen at a low angle its depth runs a few pixels of slope
    // away from the point's, so the tolerance grows with the slope of the depth on screen.
    const float tolerance = 0.03 * clip.w + 0.02 + 4.0 * fwidth(clip.w);
    const float same = saturate(2.0 - abs(occlusion.g - clip.w) / tolerance);
    return lerp(1.0, occlusion.r, same);
}
