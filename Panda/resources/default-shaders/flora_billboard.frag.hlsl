// Impostor plant billboard: color from the color atlas, silhouette and normal from the normal atlas
// (for a crown — the normal of EACH leaf, as in veg_crown; the normal is rotated by the instance
// yaw) → Cook-Torrance from the sun: the distance stays lit, not a flat sticker. Adjacent views are
// crossfaded (the weight comes from the vert): both atlases are linear, the color is premultiplied
// — the lerp is correct. Near billboards (inside Shadow Distance) receive cascade shadows from
// neighbors and the terrain — at the mesh zone border a shaded forest is not replaced by brightly
// lit stickers right next to it; the shadow is read on the tree AXIS shifted towards the sun (a
// billboard is never behind its own shadow plane — otherwise the caster plane facing the light and
// the receiver plane facing the camera would intersect along the axis and acne would flicker when
// moving). Crown self-shadowing is analytic: a cylindrical billboard implies a crown ellipsoid (the
// same as for the occlusion volume: center 0.675 H, semi-axes — the quad half-width and 0.325 H),
// convex towards the camera; the sun is attenuated by the length of the chord from a point on its
// front surface to the exit along the ray towards the sun — what CSM gives a mesh crown leaf by
// leaf, including the trunk under the crown. Fading at the impostor end — a dissolve by raising the
// silhouette alpha threshold.
// Runtime material fields (floraBillboardMaterial); the composition is the same as in the vert.
cbuffer MATERIAL_FIELDS {
    float4 sizeParams;
    float4 cameraLocal;
    float4 fadeParams;   // x — fade-out start (m), y — end
    uint doubleSided;
    float4 surfaceParams; // x — species crown metallic, y — roughness (Cook-Torrance), z — leaf translucency
};

#define PANDA_SHADOW_SINGLE_TAP // vegetation: a single compare tap (see shadow.hlsli)
#include "panda/sun.hlsli"
#include "panda/brdf.hlsli"

Texture2D imposterColorTexture;
SamplerState imposterColorTextureSampler;
Texture2D imposterNormalTexture;
SamplerState imposterNormalTextureSampler;

// Chord (m) from point p0 along ray d to the exit of the unit ellipsoid: p0 and d are in
// normalized crown coordinates (semi-axes divided out), t is in meters. Point outside and ray
// missing — 0; point on the surface and ray outward — 0 (the side facing the sun is open).
float ellipsoidChord(float3 p0, float3 d) {
    float a = dot(d, d);
    float b = 2.0 * dot(p0, d);
    float c = dot(p0, p0) - 1.0;
    float disc = b * b - 4.0 * a * c;
    if (disc <= 0.0 || a <= 1.0e-8) { return 0.0; }
    float root = sqrt(disc);
    float t1 = (-b - root) / (2.0 * a);
    float t2 = (-b + root) / (2.0 * a);
    return max(0.0, t2 - max(t1, 0.0));
}

// Sun visibility: cascade shadows of neighbors at a point on the axis (shadowPos), crown
// self-shadowing — by the chord through the ellipsoid (with the strength and distance of CSM:
// exactly where a mesh crown shadows itself with cascades), beyond Distance — only the canopy
// transmittance.
float billboardSunVisibility(float3 shadowPos, float chord, float volumeTransmittance) {
    float csmWeight;
    float shadow = sampleShadow(shadowPos, float3(0.0, 1.0, 0.0), csmWeight);
    float self = lerp(1.0, exp(-chord), shadowParams.w * csmWeight);
    // Occlusion volume: beyond the CSM distance the canopy transmittance is the only
    // light and shade of the forest.
    return lerp(1.0, shadow, csmWeight) * self * lerp(1.0, volumeTransmittance, volumeSunStrength(csmWeight));
}

struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float2 atlasUV0 : TEXCOORD1;
    float2 atlasUV1 : TEXCOORD2;
    nointerpolation float2 yawBlend : TEXCOORD3; // x — yaw, y — crossfade weight
    nointerpolation float3 tint : TEXCOORD4;     // per-instance tone jitter (vert)
    nointerpolation float3 worldBase : TEXCOORD5; // base of the tree axis (world)
    nointerpolation float2 quadSize : TEXCOORD6;  // half-width, height of the quad (world, m)
    float2 quadUV : TEXCOORD7;                    // x — across [-1..1], y — height [0..1]
};

struct PSOutput {
    float4 color : SV_Target0;
    float4 emissive : SV_Target1;
};

PSOutput main(PSInput input) {
    // The silhouette is the ALPHA OF THE COLOR ATLAS: the pbr frag writes 1 on everything drawn
    // (opaque/mask), the clear background is 0. The mask and the color live in one atlas, so
    // filtering (bilinear and mips) interpolates them consistently: at the edges the color fades
    // to the black background by exactly the same fraction as the alpha — the division restores
    // the color without a dark halo. The normal atlas alpha is not suitable as a mask: its
    // silhouette is wider than the color one. The view crossfade uses the same premultiplied lerp
    // (the weight is constant across the quad).
    const float blend = input.yawBlend.y;
    float4 colorSample = lerp(
        imposterColorTexture.Sample(imposterColorTextureSampler, input.atlasUV0),
        imposterColorTexture.Sample(imposterColorTextureSampler, input.atlasUV1),
        blend
    );
    float cameraDistance = distance(input.worldPos, cameraPosition.xyz);
    float fadeT =
        saturate((cameraDistance - fadeParams.x) / max(fadeParams.y - fadeParams.x, 0.001));
    // Dissolve: the threshold grows with distance — the foliage disintegrates instead of
    // fading out as a block. The base threshold is low: atlas filtering blurs thin foliage into
    // semi-transparent pixels — a threshold of 0.5 discarded them and the billboard crown got
    // thinner than the mesh (the premultiplied division keeps the color clean even at low alpha).
    float threshold = lerp(0.28, 1.01, fadeT);
    if (colorSample.a < threshold) { discard; }
    float3 albedo = colorSample.rgb / max(colorSample.a, 0.0001) * input.tint;
    float4 normalSample = lerp(
        imposterNormalTexture.Sample(imposterNormalTextureSampler, input.atlasUV0),
        imposterNormalTexture.Sample(imposterNormalTextureSampler, input.atlasUV1),
        blend
    );

    // Normals in the atlas are "premultiplied" by coverage (the clear background is zeros, what is
    // drawn writes α=1): filtering and mips average the encoded value with the zero background, and
    // raw decoding pulled the normal towards (−1,−1,−1) on edges and sparse foliage — the
    // billboard crown systematically got darker than the mesh. Dividing by α restores the
    // average normal; the view crossfade lerps the samples BEFORE the division — the same algebra.
    float3 raw = (normalSample.xyz / max(normalSample.a, 0.0001)) * 2.0 - 1.0;
    // Degenerate normal (an empty sample decodes to ~0) — vertical:
    // the pixel is lit like the ground, without a NaN from normalize(0).
    float rawLength = length(raw);
    float3 normal = rawLength > 0.1 ? raw / rawLength : float3(0.0, 1.0, 0.0);
    // Normals are baked in the mesh coordinate system (identity): rotation by the instance yaw
    // into the world.
    float cosYaw = cos(input.yawBlend.x);
    float sinYaw = sin(input.yawBlend.x);
    normal = float3(
        normal.x * cosYaw + normal.z * sinYaw,
        normal.y,
        -normal.x * sinYaw + normal.z * cosYaw
    );

    const float metallic = saturate(surfaceParams.x);
    const float roughness = clamp(surfaceParams.y, 0.04, 1.0);
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    const float3 viewDirection = normalize(cameraPosition.xyz - input.worldPos);
    // The implied crown ellipsoid in the quad basis (right/forward — towards the camera, as in
    // the vert): the fragment point is on its front surface inside the silhouette, and on the
    // axis plane outside it (trunk, corners). Yields the volume normal for sampling the crown
    // mass and the self-shadowing chord towards the sun (the trunk under the crown is shaded by
    // the crown itself when the sun is high).
    const float3 toCameraH =
        float3(cameraPosition.x - input.worldBase.x, 0.0, cameraPosition.z - input.worldBase.z);
    const float3 forward =
        dot(toCameraH, toCameraH) > 1.0e-6 ? normalize(toCameraH) : float3(0.0, 0.0, 1.0);
    const float3 right = float3(forward.z, 0.0, -forward.x);
    const float radiusXZ = max(input.quadSize.x, 0.01);
    const float height = max(input.quadSize.y, 0.01);
    const float radiusY = 0.325 * height;
    float3 p0 = float3(input.quadUV.x, (input.quadUV.y - 0.675) * height / radiusY, 0.0);
    const float front2 = 1.0 - dot(p0.xy, p0.xy);
    const bool inCrown = front2 > 0.0;
    p0.z = inCrown ? sqrt(front2) : 0.0;
    // Volume normal: in the crown — a sphere of normalized coordinates, at the trunk — a cylinder.
    const float3 volumeNormal = normalize(
        right * p0.x + float3(0.0, 1.0, 0.0) * (inCrown ? p0.y : 0.0) +
        forward * (inCrown ? p0.z : sqrt(saturate(1.0 - p0.x * p0.x)))
    );
    // Occlusion volume: the volume is sampled one cell outward along the volume normal, LIKE A MESH
    // (along the crown normal): the lit shell reads the volume outside itself, the interior gets
    // darker; without the shift, at the zone junction the billboard darkened by the visibility
    // difference inside its own crown.
    const float2 volume = volumeOcclusion(input.worldPos + volumeNormal * occlusionTexel.w);
    float3 color = ambientColorIntensity.rgb * ambientColorIntensity.w * albedo *
                   lerp(1.0, volume.x, occlusionParams.x);
    if (directionalDirectionIntensity.w > 0.0) {
        const float3 lightDirection = normalize(directionalDirectionIntensity.xyz);
        const float3 rayN = float3(
            dot(lightDirection, right) / radiusXZ, lightDirection.y / radiusY,
            dot(lightDirection, forward) / radiusXZ
        );
        const float chord = ellipsoidChord(p0, rayN);
        // The shadow point is on the tree axis at the fragment height, shifted towards the sun
        // past the billboard's own shadow plane (the caster is a quad facing the light through the
        // axis).
        const float2 sunH = lightDirection.xz;
        const float3 sunOffset = dot(sunH, sunH) > 1.0e-6
                                     ? normalize(float3(sunH.x, 0.0, sunH.y)) * (0.5 * radiusXZ + 1.0)
                                     : float3(0.0, 0.0, 0.0);
        const float3 shadowPos =
            float3(input.worldBase.x, input.worldPos.y, input.worldBase.z) + sunOffset;
        const float3 sunRadiance = directionalColor.rgb * directionalDirectionIntensity.w;
        const float sunLit = billboardSunVisibility(shadowPos, chord, volume.y);
        color += shadeLight(normal, viewDirection, lightDirection, sunRadiance, albedo, metallic, roughness, f0) *
                 sunLit;
        // Leaf translucency — as in veg_crown: the sun behind the leaf relative to the camera, a
        // forward lobe, the same shadow (the crown silhouette against the sun glows on the
        // billboard too).
        [branch]
        if (surfaceParams.z > 0.001) {
            const float backLit = saturate(-dot(normal, lightDirection));
            const float lobe = pow(saturate(dot(viewDirection, -lightDirection)), 3.0);
            color += sunRadiance * albedo * float3(1.0, 0.95, 0.6) *
                     (surfaceParams.z * backLit * (0.3 + 0.7 * lobe) * sunLit);
        }
    }

    PSOutput output;
    output.color = float4(color, 1.0);
    output.emissive = float4(0.0, 0.0, 0.0, 1.0);
    return output;
}
