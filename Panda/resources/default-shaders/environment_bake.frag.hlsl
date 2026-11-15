// The bake of the environment light atlas (Panda::EnvironmentLighting): one fullscreen pass over
// the atlas. A pixel finds its tile, turns its place in the tile into a direction of the
// equirectangular map and integrates the map around it: the GGX lobe of the tile's roughness
// (the prefiltered radiance) or the cosine lobe (the irradiance over pi). The map is sampled with
// filtered importance sampling: the mip of a sample follows its solid angle, so a small bright
// source — the sun of an HDR map — is smeared over the lobe instead of sparkling.

cbuffer ENVIRONMENT_BAKE {
    float4 atlasSize;     // xy — the atlas in texels, z — the gutter around a tile
    float4 sourceSize;    // xy — the map in texels, z — its highest mip
    float4 tileRects[6];  // the tile without the gutter, texels: xy — origin, zw — size
    float4 tileParams[6]; // x — roughness, y — 1 for the irradiance tile
};

Texture2D environmentMap;
SamplerState environmentMapSampler;

struct PSInput {
    float4 position : SV_POSITION;
    float2 fragTexCoord : TEXCOORD0;
};

static const float PI = 3.14159265359;
static const uint SAMPLES = 128u;

// The direction of a place in an equirectangular tile (the engine's layout: the top row is the
// zenith, u = atan2(z, x) / 2pi + 0.5). u past the edges wraps by itself, v is clamped by the caller.
float3 directionOf(float2 uv) {
    const float phi = (uv.x - 0.5) * 2.0 * PI;
    const float elevation = (0.5 - uv.y) * PI;
    const float horizontal = cos(elevation);
    return float3(horizontal * cos(phi), sin(elevation), horizontal * sin(phi));
}

float2 uvOf(float3 direction) {
    return float2(atan2(direction.z, direction.x), asin(clamp(-direction.y, -1.0, 1.0))) * float2(0.1591549, 0.3183099) + 0.5;
}

float radicalInverse(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

// A sample of the map along a direction whose probability density is `pdf`: the mip whose texel
// covers the solid angle the sample stands for.
float3 sampleSource(float3 direction, float pdf) {
    const float texelSolidAngle = 4.0 * PI / (sourceSize.x * sourceSize.y);
    const float sampleSolidAngle = 1.0 / (float(SAMPLES) * max(pdf, 1.0e-4));
    const float mip = clamp(0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0, 0.0, sourceSize.z);
    return environmentMap.SampleLevel(environmentMapSampler, uvOf(direction), mip).rgb;
}

float4 main(PSInput input) : SV_Target0 {
    const float2 pixel = input.fragTexCoord * atlasSize.xy;
    const float gutter = atlasSize.z;
    int tile = -1;
    for (int index = 0; index < 6; ++index) {
        const float4 rect = tileRects[index];
        if (pixel.x >= rect.x - gutter && pixel.x < rect.x + rect.z + gutter && pixel.y >= rect.y - gutter &&
            pixel.y < rect.y + rect.w + gutter) {
            tile = index;
        }
    }
    if (tile < 0) { return float4(0.0, 0.0, 0.0, 1.0); }

    const float4 rect = tileRects[tile];
    float2 uv = (pixel - rect.xy) / rect.zw;
    uv.y = saturate(uv.y); // past a pole the gutter repeats the pole row
    const float3 normal = directionOf(uv);
    // A frame around the direction. The lobe is symmetric, so any tangent serves.
    const float3 up = abs(normal.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(up, normal));
    const float3 bitangent = cross(normal, tangent);

    const bool irradiance = tileParams[tile].y > 0.5;
    const float roughness = tileParams[tile].x;
    const float alpha = roughness * roughness;
    float3 sum = float3(0.0, 0.0, 0.0);
    float weight = 0.0;
    for (uint i = 0u; i < SAMPLES; ++i) {
        const float2 xi = float2((float(i) + 0.5) / float(SAMPLES), radicalInverse(i));
        const float phi = 2.0 * PI * xi.x;
        if (irradiance) {
            // Cosine-weighted hemisphere: pdf = cos / pi, every sample weighs the same.
            const float cosTheta = sqrt(1.0 - xi.y);
            const float sinTheta = sqrt(xi.y);
            const float3 light = tangent * (sinTheta * cos(phi)) + bitangent * (sinTheta * sin(phi)) + normal * cosTheta;
            sum += sampleSource(light, cosTheta / PI);
            weight += 1.0;
        } else {
            // The GGX distribution of half vectors around the direction (the view is taken along
            // it: the split-sum approximation), weighted by N.L as the lobe of the reflection is.
            const float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (alpha * alpha - 1.0) * xi.y));
            const float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
            const float3 halfVector =
                tangent * (sinTheta * cos(phi)) + bitangent * (sinTheta * sin(phi)) + normal * cosTheta;
            const float3 light = 2.0 * dot(normal, halfVector) * halfVector - normal;
            const float nDotL = dot(normal, light);
            if (nDotL > 0.0) {
                const float a2 = alpha * alpha;
                const float denominator = cosTheta * cosTheta * (a2 - 1.0) + 1.0;
                const float distribution = a2 / (PI * denominator * denominator);
                // pdf of the light direction: D * N.H / (4 * V.H), and N.H = V.H here.
                sum += sampleSource(light, distribution * 0.25) * nDotL;
                weight += nDotL;
            }
        }
    }
    return float4(sum / max(weight, 1.0e-4), 1.0);
}
