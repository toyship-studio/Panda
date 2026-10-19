#include "panda/vertex_ubo.hlsli"

cbuffer MATERIAL_FIELDS {
    float4 colorRoot;
    float4 colorTip;
    float4 colorExtra; // color of random blades (dry grass blades)
    float4 windParams; // xy — wind direction (XZ), z — strength, w — flutter phase (CPU)
    float4 timeFade;   // x — gust travel (m), y — fade start, z — fade end, w — gustiness
    float4 fadeCamera; // xyz — camera position in terrain LOCAL space (Euclidean fade + LOD prefix on CPU)
    uint doubleSided;
};

// 1D value noise with a PERIOD of period (in cells): random nodes, smoothstep between
// them — a smooth but NON-PERIODIC signal (sine interference read as "machine-made
// waves", a white hash over time — as jitter; the veg_crown foliage model). The phase travels
// are accumulated on the CPU with an fmod 3003 wrap — the hash is taken from the wrapped cell
// (fmod is signed: t and t−3003 give the same cell), the wrap is seamless, and the sin argument
// in the hash is always small — fast-math at hundreds of radians does not degrade with session
// time.
float windNoise(float t, float period) {
    const float f = frac(t);
    const float cell = fmod(floor(t), period);
    const float a = frac(sin(cell * 12.9898) * 43758.5453);
    const float b = frac(sin(fmod(cell + 1.0, period) * 12.9898) * 43758.5453);
    return lerp(a, b, f * f * (3.0 - 2.0 * f));
}

// Scene wind strength at a point — layers 1+2 (the third one, the per-tuft flutter, is in main).
// Layer 1 — a GLOBAL envelope: the whole scene strengthens and calms down together (periods
// ~6/15 s, the signal is the CPU flutter phase). Layer 2 — two NON-COAXIAL travelling value
// noises (waves of 21 and 11 m: along the wind and at ~29° to it) → patches of different shapes
// drifting across the field with no visible period, they deflect the strength around the
// envelope. The wavelengths are divisors of the wrap period 3003 (see windNoise:
// 3003 = 3×7×11×13). travelled — the patch travel accumulated on the CPU (m), gustiness — the
// gustiness of the scene (the depth of the lulls and the range of the deviations).
float gustField(float2 p, float2 windDir, float travelled, float flutterTime, float gustiness) {
    const float envelope = lerp(0.5, smoothstep(0.15, 0.85,
        windNoise(flutterTime / 13.0, 231.0) * 0.65 +
        windNoise(flutterTime / 33.0 + 7.7, 91.0) * 0.35), gustiness);
    const float2 side = normalize(windDir + float2(-windDir.y, windDir.x) * 0.55);
    const float wave = windNoise((dot(p, windDir) - travelled) / 21.0, 143.0);
    const float wave2 = windNoise((dot(p, side) - travelled) / 11.0 + 61.8, 273.0);
    const float local =
        (smoothstep(0.3, 0.85, wave * 0.62 + wave2 * 0.38) - 0.5) * (0.4 + 0.6 * gustiness);
    return saturate(envelope + local);
}

struct VSInput {
    float3 position : POSITION;  // blade vertex in tuft local space (height 1)
    float h : TEXCOORD0;         // 0 root .. 1 tip — bend factor
    float2 spreadDir : TEXCOORD1; // spread direction of the blade (XZ)
    float phase : TEXCOORD2;     // phase shift of the blade within the tuft
    // Instance data (second vertex stream, per-instance).
    float4 iPosScale : TEXCOORD3; // xyz — tuft root in terrain local space, w — scale
    float4 iParams : TEXCOORD4;   // x — yaw, y — wind phase, z — tint, w — thinning threshold
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float3 hTint : TEXCOORD2; // x — vertex h, y — tuft tint, z — blade random (third color)
    float3 rootPos : TEXCOORD3; // tuft root in world space — CSM shadow is sampled here, not on the swaying blade
};

VSOutput main(VSInput input) {
    VSOutput output;
    const float yaw = input.iParams.x;
    const float s = sin(yaw);
    const float c = cos(yaw);

    // Tuft rotation around Y and scale.
    float3 local = float3(
        input.position.x * c - input.position.z * s,
        input.position.y,
        input.position.x * s + input.position.z * c
    );
    float2 spread = float2(
        input.spreadDir.x * c - input.spreadDir.y * s, input.spreadDir.x * s + input.spreadDir.y * c
    );

    // Fade at the far boundary: tufts collapse one by one at a stable threshold
    // (no flicker when the camera moves), the remaining ones shrink smoothly. The distance is
    // Euclidean in terrain local space: it is consistent with the CPU instance prefix (LOD) and
    // does not "breathe" when the camera rotates, unlike the clip depth.
    const float3 root = input.iPosScale.xyz;
    const float4 worldRoot = mul(modelMtx, float4(root, 1.0));
    const float cameraDistance = distance(root, fadeCamera.xyz);
    const float fadeT =
        saturate((cameraDistance - timeFade.y) / max(timeFade.z - timeFade.y, 0.001));
    // A tuft shrinks into its root over the 15% of the fade zone before its threshold — no pops;
    // the CPU prefix of the block cuts the instance only past the threshold, when it is already
    // at zero.
    const float sink = smoothstep(input.iParams.w - 0.15, input.iParams.w, fadeT);
    const float fadeScale = (1.0 - 0.35 * fadeT) * (1.0 - sink);
    const float scale = input.iPosScale.w * fadeScale;
    local *= scale;

    // Wind — the veg_crown foliage model (accepted by the user): STRENGTH DRIVES THE ENERGY, NOT
    // THE LEAN. The mean deflection saturates early — the grass leans but does not lie down
    // (previously the strength multiplied the deflection directly, at the lodging ceiling the
    // oscillations were clipped: "lies down and stays down"). A gust swings the deflection around
    // the mean and BOOSTS the per-tuft flutter. The common motion is only the travelling gust
    // patches, without random phases (neighbors out of sync in the common motion = chaos);
    // the individuality is in the flutter and the stiffness of the tuft. All phases are
    // accumulated on the CPU — editing the wind settings does not teleport the picture.
    const float2 windDir = windParams.xy;
    const float gust = gustField(worldRoot.xz, windDir, timeFade.x, windParams.w, timeFade.w);
    // Tuft stiffness — a deterministic function of the tint hash (stiff ones bend less).
    const float stiffness = 0.75 + 0.5 * frac(input.iParams.z * 7.13);
    // Mean deflection: a saturating curve of the strength, the limit is ~34% of the height —
    // beyond that the strength adds motion, not lean.
    const float lean = 0.34 * windParams.z / (1.0 + windParams.z) / stiffness;
    // Tuft flutter: value noise over CPU time (windParams.w) with a per-tuft phase,
    // the second octave ×2 — liveliness; sideways — its own independent pattern of the same
    // frequency. A gust boosts the amplitude ~×8: a lull is almost motionless, a squall boils up.
    const float flutterPhase = input.iParams.y * 1.7 + input.phase * 2.3;
    const float flutter =
        (windNoise(windParams.w + flutterPhase * 9.7, 3003.0) * 2.0 - 1.0) +
        (windNoise(windParams.w * 2.0 + flutterPhase * 13.1, 6006.0) * 2.0 - 1.0) * 0.5;
    const float flutterSide =
        windNoise(windParams.w + flutterPhase * 7.9 + 41.0, 3003.0) * 2.0 - 1.0;
    const float flutterAmp = lean * (0.12 + 0.88 * gust) * 0.45;
    const float bend = input.h * input.h * scale;
    // Static "unkemptness": each tuft is slightly tilted in its own random
    // direction (sin/cos of yaw are used — the direction is already random) — the field
    // stops looking combed. fadeCamera.w — the brownian strength from the settings.
    const float tilt = (0.03 + 0.11 * frac(input.iParams.z * 5.37)) * fadeCamera.w;
    local.xz += float2(c, s) * (tilt * bend);
    // Deflection: the mean lean swung by the gust (lull ~0.6×, squall ~1.5×) + flutter.
    // The soft saturation (ceiling ~80% of the height) remains as a safeguard against a "break",
    // but the operating point is now far from it — the oscillations are not clipped.
    const float rawDisplacement = (lean * (0.6 + 0.9 * gust) + flutter * flutterAmp) * bend;
    const float maxBend = 0.8 * max(input.h * scale, 0.001);
    const float displacement = rawDisplacement / (1.0 + rawDisplacement / maxBend);
    local.xz += windDir * displacement + spread * (flutterSide * flutterAmp * 0.5 * bend);
    // Arc: when bending, the tip goes down (the stem length is preserved).
    local.y -= displacement * displacement * 0.6 / max(scale, 0.05);

    const float4 worldPos = mul(modelMtx, float4(root + local, 1.0));
    output.position = mul(projViewMtx, worldPos);
    output.worldPos = worldPos.xyz;
    // The normal is almost vertical with a slight outward tilt: the tuft is lit as a volume.
    const float3 normal = normalize(float3(spread.x * 0.45, 1.0, spread.y * 0.45));
    output.worldNormal = normalize(mul((float3x3) modelMtx, normal));
    // Blade random for the third color: blade phase × tuft tint — a stable pair.
    const float bladeRand = frac(input.phase * 1.618 + input.iParams.z * 7.77);
    // The shrinkage "sinks" the tuft into the ground: the upper part of the blade is visible, so
    // the gradient is read by the ORIGINAL height (h of the compressed blade → 1 − fadeScale .. 1),
    // not by the compressed one — otherwise the shrinkage band darkens to the root color.
    output.hTint = float3(lerp(1.0 - fadeScale, 1.0, input.h), input.iParams.z, bladeRand);
    output.rootPos = worldRoot.xyz;
    return output;
}
