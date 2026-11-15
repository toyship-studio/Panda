// The fog of a world (Panda::FogRenderer, "graphics.fog" of .world): an exponential height fog
// over the finished scene, in linear HDR. The density of the air falls with the height
// (halving every heightFalloff meters), so the amount of air along the view ray has a closed
// form. The fog is lit by the world: it shows the sky of the world near the horizon in the
// direction of the view and glows around the sun.

#if PANDA_FEATURE_LIGHTS
#    include "panda/lights.hlsli"
#endif
#if PANDA_FEATURE_ENVIRONMENT
#    include "panda/environment.hlsli"
#endif
#include "panda/view.hlsli"

cbuffer FOG_PASS {
    float4x4 inverseViewProj;
    float4 density;  // x — density at the base height (1/m), y — ln 2 / heightFalloff (0 — uniform), z — base height, w — start distance
    float4 fogColor; // rgb — tint, a — sun scatter
    float4 params;   // x — max opacity, y — 1: the sky is fogged, z — 1: depth is -1..1, w — 1: rows go bottom-up
};

Texture2D inputTexture;
SamplerState inputTextureSampler;
Texture2D depthTexture;
SamplerState depthTextureSampler;

struct PSInput {
    float4 position : SV_POSITION;
    float2 fragTexCoord : TEXCOORD0;
};

static const float SKY_DISTANCE = 1.0e5; // the sky is behind this much air (m)

// The Henyey-Greenstein phase: how much of the sun's light the fog turns towards the eye.
float phase(float cosine, float g) {
    const float g2 = g * g;
    return (1.0 - g2) / (12.5663706 * pow(max(1.0 + g2 - 2.0 * g * cosine, 1.0e-4), 1.5));
}

float4 main(PSInput input) : SV_Target0 {
    const float2 uv = input.fragTexCoord;
    const float4 scene = inputTexture.SampleLevel(inputTextureSampler, uv, 0.0);
    const float depth = depthTexture.SampleLevel(depthTextureSampler, uv, 0.0).r;
    const bool sky = (params.z > 0.5 ? depth * 0.5 + 0.5 : depth) >= 0.99999;
    if (sky && params.y < 0.5) { return scene; }

    // The point of the world behind the pixel.
    const float2 ndc = float2(uv.x * 2.0 - 1.0, (params.w > 0.5 ? uv.y : 1.0 - uv.y) * 2.0 - 1.0);
    const float4 clip = float4(ndc, params.z > 0.5 ? depth * 2.0 - 1.0 : depth, 1.0);
    // A point of the near plane gives the direction of the ray also for the sky, whose own point
    // is at the limit of the depth.
    const float4 nearPoint = mul(inverseViewProj, float4(ndc, params.z > 0.5 ? -1.0 : 0.0, 1.0));
    const float4 farPoint = mul(inverseViewProj, clip);
    const float3 origin = cameraPosition.xyz;
    const float3 toPoint = farPoint.xyz / farPoint.w - origin;
    const float3 direction = sky ? normalize(nearPoint.xyz / nearPoint.w - origin) : normalize(toPoint);
    const float distance = max((sky ? SKY_DISTANCE : length(toPoint)) - density.w, 0.0);

    // The air along the ray: density(h) = d0 * exp(-k (h - base)), integrated over the distance.
    const float k = density.y;
    const float startHeight = origin.y + direction.y * density.w - density.z;
    const float slope = k * direction.y * distance;
    // (1 - exp(-x)) / x, with its limit 1 at x = 0 (a level ray, a uniform fog).
    const float along = abs(slope) > 1.0e-4 ? (1.0 - exp(-slope)) / slope : 1.0 - slope * 0.5;
    const float air = density.x * exp(-clamp(k * startHeight, -40.0, 40.0)) * distance * along;
    const float opacity = min(1.0 - exp(-air), params.x);
    if (opacity <= 0.0) { return scene; }

    // The light the fog scatters towards the eye. From the sky: the sky of the world low over
    // the horizon in the direction of the view (the widest lobe of the baked environment light),
    // so the fog meets the horizon of the sky map by construction.
    const float3 horizon = normalize(float3(direction.x, max(direction.y, 0.0) * 0.3 + 0.03, direction.z));
    float3 light = float3(0.5, 0.5, 0.5);
#if PANDA_FEATURE_LIGHTS
    light = ambientColorIntensity.rgb * ambientColorIntensity.a;
#endif
#if PANDA_FEATURE_ENVIRONMENT
    if (environmentParams.z > 0.5) { light = environmentTile(4, environmentUv(horizon)) * environmentParams.x; }
#endif
    light *= fogColor.rgb;
#if PANDA_FEATURE_LIGHTS
    // From the sun: forward scattering, a glow around the sun that widens with the fog.
    const float towardsSun = dot(direction, normalize(sunDirectionIntensity.xyz));
    light += sunColor.rgb * sunDirectionIntensity.w * phase(towardsSun, 0.6) * fogColor.a;
#endif
    return float4(lerp(scene.rgb, light, opacity), scene.a);
}
