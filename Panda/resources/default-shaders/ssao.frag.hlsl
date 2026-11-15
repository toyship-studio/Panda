// Screen-space ambient occlusion, the integral (Panda::SsaoRenderer): the share of the sky a
// point of the frame sees past the geometry around it. GTAO (Jimenez, Wu, Pesce, Jarabo,
// "Practical Real-Time Strategies for Accurate Indirect Occlusion"): the hemisphere is cut into
// slices through the view direction; in a slice the depth buffer is walked both ways from the
// point for the highest horizon, and the visible arc between the two horizons is integrated with
// the cosine of the normal. A few slices and steps per pixel, turned from pixel to pixel; the
// blur pass (ssao_blur.frag) removes the grain. Runs at half resolution.
// Output: r — the visible share 0..1, g — the view depth of the point (m).

cbuffer SSAO_PASS {
    float4 projection; // x, y — the scale of the projection, z — 1: depth is -1..1, w — 1: rows go bottom-up
    float4 params;     // x — radius (m), y — intensity, z — 1 / near, w — 1 / far
    float4 size;       // xy — the size of the depth in pixels, zw — of the target
};

Texture2D depthTexture;
SamplerState depthTextureSampler;

struct PSInput {
    float4 position : SV_POSITION;
    float2 fragTexCoord : TEXCOORD0;
};

static const float PI = 3.14159265359;
static const float HALF_PI = 1.57079632679;
static const int SLICES = 3;
static const int STEPS = 4;

// The point of the frame at a uv, in view space (x right, y up, -z forward). w — 0 for the sky.
// The depth is read at a texel, so the point is the one at the center of that texel: taken at
// the uv itself it would slide by up to half a texel against its depth, and a flat floor seen
// at a low angle would shade itself in bands.
float4 viewPoint(float2 uv) {
    uv = (floor(uv * size.xy) + 0.5) / size.xy;
    const float depth = depthTexture.SampleLevel(depthTextureSampler, uv, 0.0).r;
    // The distance along the view axis: 1 / z runs linearly from 1 / near to 1 / far with the
    // depth. Written as a sum of two positive terms — `1 - depth` is exact, and a far point is
    // not the small difference of two large numbers the inverse projection matrix would make of
    // it (that noise bends the normal of a floor seen at a low angle).
    const float t = projection.z > 0.5 ? depth * 0.5 + 0.5 : depth;
    const float viewDepth = 1.0 / ((1.0 - t) * params.z + t * params.w);
    const float2 ndc = float2(uv.x * 2.0 - 1.0, (projection.w > 0.5 ? uv.y : 1.0 - uv.y) * 2.0 - 1.0);
    return float4(ndc * viewDepth / projection.xy, -viewDepth, t < 0.99999 ? 1.0 : 0.0);
}

// A number 0..1 that takes each of 25 values once in every 5×5 block of pixels: the blur pass
// averages a 5×5 neighborhood, so what is turned by it from pixel to pixel averages out exactly
// on a flat surface.
float blockNoise(float2 pixel, float shift) {
    const float2 cell = fmod(floor(pixel), 5.0);
    return fmod((cell.x + 5.0 * cell.y) * 7.0 + shift, 25.0) / 25.0;
}

float4 main(PSInput input) : SV_Target0 {
    // The pixel of this pass covers 2×2 texels of the depth and its center lies on their common
    // corner: which of the four a read there takes is a matter of rounding, and so would be
    // whether the neighbor one texel aside is another texel at all. The point is the first of
    // the four, addressed by its center.
    const float2 uv = (floor(input.position.xy) * 2.0 + 0.5) / size.xy;
    const float4 center = viewPoint(uv);
    if (center.w < 0.5) { return float4(1.0, 1.0e4, 0.0, 1.0); }
    const float3 position = center.xyz;
    const float viewDepth = -position.z;
    const float3 viewDirection = normalize(-position);

    // The normal from the depth: of the two neighbors along an axis, the one on the same surface
    // (the smaller step in depth), so an edge does not bend it.
    const float2 texel = 1.0 / size.xy;
    const float3 right = viewPoint(uv + float2(texel.x, 0.0)).xyz;
    const float3 left = viewPoint(uv - float2(texel.x, 0.0)).xyz;
    const float3 down = viewPoint(uv + float2(0.0, texel.y)).xyz;
    const float3 up = viewPoint(uv - float2(0.0, texel.y)).xyz;
    const float3 alongX = abs(right.z - position.z) < abs(position.z - left.z) ? right - position : position - left;
    const float3 alongY = abs(down.z - position.z) < abs(position.z - up.z) ? down - position : position - up;
    float3 normal = normalize(cross(alongX, alongY));
    if (dot(normal, viewDirection) < 0.0) { normal = -normal; }

    // The radius on screen, in uv. Too small to hold a pixel — the point is open.
    const float radius = params.x;
    const float2 uvRadius = radius * 0.5 * projection.xy / viewDepth;
    if (uvRadius.x * size.x < 1.5) { return float4(1.0, viewDepth, 0.0, 1.0); }
    const float rowSign = projection.w > 0.5 ? 1.0 : -1.0; // uv.y against the y of the view
    // An occluder fades out over the far part of the radius instead of popping at its edge.
    const float falloffRange = 0.6 * radius;
    const float falloffFrom = radius - falloffRange;

    const float noiseSlice = blockNoise(input.position.xy, 0.0);
    const float noiseStep = blockNoise(input.position.xy.yx, 9.0);
    float visibility = 0.0;
    for (int slice = 0; slice < SLICES; ++slice) {
        const float phi = (float(slice) + noiseSlice) / float(SLICES) * PI;
        const float2 omega = float2(cos(phi), sin(phi));
        const float3 direction = float3(omega, 0.0);
        const float3 orthoDirection = direction - dot(direction, viewDirection) * viewDirection;
        const float3 axis = normalize(cross(direction, viewDirection));
        const float3 projectedNormal = normal - axis * dot(normal, axis);
        const float projectedLength = max(length(projectedNormal), 1.0e-5);
        const float signNormal = sign(dot(orthoDirection, projectedNormal));
        const float cosNormal = saturate(dot(projectedNormal, viewDirection) / projectedLength);
        const float n = signNormal * acos(cosNormal);
        // The horizons start at the tangent plane of the point.
        const float lowHorizon0 = cos(n + HALF_PI);
        const float lowHorizon1 = cos(n - HALF_PI);
        float horizon0 = lowHorizon0;
        float horizon1 = lowHorizon1;
        for (int step = 0; step < STEPS; ++step) {
            float s = (float(step) + noiseStep) / float(STEPS);
            s = s * s; // denser near the point: the contact matters most
            const float2 offset = max(s, 1.5 / (uvRadius.x * size.x)) * uvRadius * float2(omega.x, omega.y * rowSign);

            const float4 sample0 = viewPoint(uv + offset);
            const float3 delta0 = sample0.xyz - position;
            const float distance0 = length(delta0);
            const float weight0 = saturate((radius - distance0) / falloffRange) * sample0.w;
            horizon0 = max(horizon0, lerp(lowHorizon0, dot(delta0 / distance0, viewDirection), weight0));

            const float4 sample1 = viewPoint(uv - offset);
            const float3 delta1 = sample1.xyz - position;
            const float distance1 = length(delta1);
            const float weight1 = saturate((radius - distance1) / falloffRange) * sample1.w;
            horizon1 = max(horizon1, lerp(lowHorizon1, dot(delta1 / distance1, viewDirection), weight1));
        }
        // The arc between the horizons, clamped to the hemisphere of the normal, integrated with
        // the cosine.
        float h0 = -acos(clamp(horizon1, -1.0, 1.0));
        float h1 = acos(clamp(horizon0, -1.0, 1.0));
        h0 = n + clamp(h0 - n, -HALF_PI, HALF_PI);
        h1 = n + clamp(h1 - n, -HALF_PI, HALF_PI);
        const float arc0 = (cosNormal + 2.0 * h0 * sin(n) - cos(2.0 * h0 - n)) * 0.25;
        const float arc1 = (cosNormal + 2.0 * h1 * sin(n) - cos(2.0 * h1 - n)) * 0.25;
        visibility += projectedLength * (arc0 + arc1);
    }
    visibility = saturate(visibility / float(SLICES));
    return float4(visibility, viewDepth, 0.0, 1.0);
}
