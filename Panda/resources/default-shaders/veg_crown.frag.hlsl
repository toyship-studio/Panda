// Crown cards carry a GPU-baked shoot tile: baseColorTexture is sRGB albedo and a coverage
// mask (baseColorFactor tints, instances add a per-tree tone variation); normalTexture (the
// regular slot, as in any PBR) is RGB: the normal of EACH leaf/needle from the baker's 3D mesh
// in card space (X right, Y up, Z outward), A is the stable random of the leaf (wind phases).
// Tuft variability is a per-card rotation/UV mirroring baked into the vertices.
// The MATERIAL_FIELDS layout = the pbr set + the crown fields AT THE END (std140 order):
// the prefix matches PBR — the impostor normal pass binds the material fields as is and
// cuts out the silhouette by the same alpha.
// Lighting: the normal of EACH leaf is the baked tile normal through the tangent basis of the
// card PLANE (strength — the regular normalScale) at any distance: damping the per-leaf detail
// by mip (leafDetail) made distant groves flat — the leaf mottling in the distance is exactly
// the look of the crown (removing the damping adds no noise, verified). The crown VOLUME normal
// from the vertices serves only as the normal offset of shadows. Diffuse/specular — Cook-Torrance
// from the material's metallic/roughness, the per-leaf wind flutter — independently. The impostor
// bakes the same leaf normal (flora_imposter_normal_crown.frag is a copy of this branch).

cbuffer MATERIAL_FIELDS {
    float4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    float4 emissiveFactor;
    uint alphaMode;
    float alphaCutoff;
    uint doubleSided;
    float emissiveIntensity;
    // Dissolve of cards turned edge-on to the camera (0 — off): without it the edge
    // reads as a "glowing line". The field name is a contract with the impostor bake: it mutes
    // viewFade during baking (8 ortho views would bake holes into the atlas permanently).
    float viewFade;
    // Foliage wind — a per-leaf oscillation of the NORMAL (see the branch in main): the geometry
    // is static, the shadows and the silhouette are stable. Strength and gustiness are SYSTEM
    // fields (same-named fields of all materials are set by the engine — the scene wind SceneWind);
    // during the impostor bake the engine mutes windStrength (0 disables the branch).
    float windStrength;  // strength: the flutter share in calm and the amplitude in a gust
    float windGustiness; // gust contrast: 0 — a steady flow, 1 — lulls and squalls
    float canopyNormal;  // leaf lighting basis: 0 — card plane, 1 — crown volume normal
    float translucency;  // leaf translucency: share of sun through the blade when the sun is behind the leaf
};

#include "panda/panda_fields.hlsli"

#define PANDA_SHADOW_SINGLE_TAP // vegetation: a single compare tap (see shadow.hlsli)
#include "panda/sun.hlsli"
#include "panda/brdf.hlsli"

Texture2D baseColorTexture;
SamplerState baseColorTextureSampler;
// The shoot normal tile is in the REGULAR normalTexture slot of the material (as in all PBR):
// RGB is the leaf/needle normal in card space, A is the random of the leaf.
Texture2D normalTexture;
SamplerState normalTextureSampler;

struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float2 fragTexCoord : TEXCOORD2;
    float fragLight : TEXCOORD4;
    nointerpolation float3 fragTint : TEXCOORD6; // per-instance tone jitter (vert)
    float fragGust : TEXCOORD7; // gust energy — from the vertex shader (smooth over the world)
};

float3 hash3(float n) {
    return frac(sin(float3(n, n + 1.7, n + 2.8)) * float3(43758.5453, 22578.145, 19642.349));
}

// 1D value noise with a PERIOD of period (in cells): random nodes, smoothstep between them.
// The spectrum is "wind-like" — smooth but non-periodic: a pure sine reads as "machine-made
// waves", and a white hash over time reads as jitter (both mistakes have already shown up on
// grass). The phases are accumulated on the CPU with an fmod 3003 wrap (SceneWind) —
// periodizing the hash to the wave period makes the wrap seamless and the sin argument small.
// Layers 1+2 (gust) are computed by the vertex shader (windGustEnergy) and passed as fragGust;
// here there is only the flutter.
float windNoise(float t, float period) {
    const float f = frac(t);
    const float cell = fmod(floor(t), period);
    const float a = frac(sin(cell * 12.9898) * 43758.5453);
    const float b = frac(sin(fmod(cell + 1.0, period) * 12.9898) * 43758.5453);
    return lerp(a, b, f * f * (3.0 - 2.0 * f));
}

struct PSOutput {
    float4 color : SV_Target0;
    float4 emissive : SV_Target1;
};

// Tangent basis of the card from derivatives (cotangent frame, Schüler): the vertices carry
// only the CROWN normal — the basis of the card plane is reconstructed in place
// (it also accounts for the per-card UV mirroring).
float3x3 cotangentFrame(float3 normal, float3 dp1, float3 dp2, float2 duv1, float2 duv2) {
    const float3 dp2perp = cross(dp2, normal);
    const float3 dp1perp = cross(normal, dp1);
    const float3 tangent = dp2perp * duv1.x + dp1perp * duv2.x;
    const float3 bitangent = dp2perp * duv1.y + dp1perp * duv2.y;
    const float invMax = rsqrt(max(dot(tangent, tangent), dot(bitangent, bitangent)) + 1.0e-12);
    return float3x3(tangent * invMax, bitangent * invMax, normal);
}

PSOutput main(PSInput input) {
    // Capture derivatives before cutout; they drive mip selection and view fade.
    const float3 positionDx = ddx(input.worldPos);
    const float3 positionDy = ddy(input.worldPos);
    const float2 uvDx = ddx(input.fragTexCoord);
    const float2 uvDy = ddy(input.fragTexCoord);
    const float4 tile = baseColorTexture.Sample(baseColorTextureSampler, input.fragTexCoord);

    // Mip compensation (Castano): the alpha averaged with the background drops below the
    // cutoff at a distance — the crown would go bald with distance. The mip comes from the UV
    // derivatives manually.
    float2 tileTexSize;
    baseColorTexture.GetDimensions(tileTexSize.x, tileTexSize.y);
    const float2 dx = uvDx * tileTexSize;
    const float2 dy = uvDy * tileTexSize;
    const float mip = 0.5 * log2(max(max(dot(dx, dx), dot(dy, dy)), 1.0));
    const float alpha = tile.a * (1.0 + mip * 0.25);
    if (alpha < alphaCutoff) {
        discard;
    }

    // The crown VOLUME normal (the tree ellipsoid, baked by TreeBuilder): the shadow normal
    // offset and the fallback for a degenerate plane; the lighting uses the leaf normal (below).
    // Not flipped — a volume has no back side.
    const float3 crownNormal = normalize(input.worldNormal);
    const float3 viewDirection = normalize(cameraPosition.xyz - input.worldPos);
    // The card PLANE normal comes from derivatives (the vertex one is the CROWN normal, it does
    // not describe the plane); flipped towards the camera: foliage is two-sided, the back side
    // is lit symmetrically.
    const float3 faceCross = cross(positionDx, positionDy);
    const float faceLength2 = dot(faceCross, faceCross);
    float3 faceNormal = faceLength2 > 1.0e-20 ? faceCross * rsqrt(faceLength2) : crownNormal;
    if (dot(faceNormal, viewDirection) < 0.0) { faceNormal = -faceNormal; }
    // View fade of edge-on cards: the dissolve is a dither over the tile TEXELS at the current
    // mip: a cell ≈ a screen pixel at any distance (a fixed 48 cells per tile read as squares
    // on large cards), the pattern is pinned to the foliage and does not reshuffle when the camera
    // moves — it is rearranged only at a whole-mip boundary, which is not noticeable inside
    // the dissolve band; right at the edge — a guaranteed discard. Transparency instead of a
    // dither is impossible: masked foliage has no sorting, semi-transparent cards read as ghosts.
    [branch]
    if (viewFade > 0.001) {
        const float facing = abs(dot(faceNormal, viewDirection));
        if (facing < viewFade * 0.25) {
            discard;
        }
        const float fade = smoothstep(viewFade * 0.25, viewFade, facing);
        const float2 ditherCells = max(tileTexSize * exp2(-floor(mip)), 8.0);
        const float2 ditherCell = floor(input.fragTexCoord * ditherCells);
        const float ditherNoise =
            frac(52.9829189 * frac(dot(ditherCell, float2(0.06711056, 0.00583715))));
        if (fade < ditherNoise) {
            discard;
        }
    }

    // Leaf/needle normal: the normal tile in card space (X along +u, Y is card UP = −v of the
    // texture, Z out of the plane) → world through the tangent basis of the card plane; the
    // strength is the regular normalScale (XY, as in PBR). The mip is the real one, from the UV
    // derivatives taken before discard (after it the implicit derivatives are undefined):
    // normals are averaged over the pixel footprint. A clamp at level 2 (for the sake of relief
    // in the distance) read the 512 px tile 3–10 times finer than a pixel over the whole mesh
    // zone — the leaf lighting flickered when the camera moved.
    const float4 normalSample =
        normalTexture.SampleGrad(normalTextureSampler, input.fragTexCoord, uvDx, uvDy);
    float3 tileNormal = normalSample.rgb * 2.0 - 1.0;
    tileNormal.xy *= normalScale;
    tileNormal.y = -tileNormal.y;
    // Leaf lighting basis: the card plane (two-sided, towards the camera) ↔ the crown VOLUME
    // normal by canopyNormal — at 1 the foliage is lit as one common mass (branch cards
    // do not read individually after baking), the tile relief is laid on top.
    const float3 mixedBasis = lerp(faceNormal, crownNormal, saturate(canopyNormal));
    const float3 shadingBasis = dot(mixedBasis, mixedBasis) > 1.0e-6 ? normalize(mixedBasis) : faceNormal;
    const float3x3 cardFrame = cotangentFrame(shadingBasis, positionDx, positionDy, uvDx, uvDy);
    float3 leafNormal = normalize(mul(tileNormal, cardFrame));
    // Wind: a per-leaf flutter of the normal (the geometry is static), the phase is the leaf
    // random from the tile alpha; the gust energy (layers 1+2) comes from the vertex shader —
    // only layer 3 here.
    [branch]
    if (windStrength > 0.001) {
        const float leafRand = normalSample.a;
        const float gust = saturate(input.fragGust);
        const float flutterPhase = leafRand * 157.0;
        const float flutter =
            (windNoise(sceneWind.w * 2.0 + flutterPhase, 6006.0) * 2.0 - 1.0) +
            (windNoise(sceneWind.w * 5.0 + flutterPhase * 1.7, 15015.0) * 2.0 - 1.0) * 0.5;
        const float swing = windStrength * (0.12 + 0.88 * gust);
        const float3 flutterAxis = hash3(leafRand * 431.7) * 2.0 - 1.0;
        leafNormal = normalize(leafNormal + flutterAxis * (flutter * swing * 0.45));
    }
    // Undo the shoot tile's brightness normalization (CROWN_BRIGHTNESS_GAIN).
    const float3 albedo = baseColorFactor.rgb * input.fragTint * tile.rgb * 1.28;
    const float metallic = saturate(metallicFactor);
    const float roughness = clamp(roughnessFactor, 0.04, 1.0);
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    // Occlusion volume: sky visibility attenuates ambient, transmittance attenuates the sun. The
    // volume is sampled one CELL outward along the crown normal (starting from the surface, like
    // a DFAO cone): the lit shell of the crown reads the volume outside itself — only neighbors
    // cut the sky and the sun for it, while the leaves in the interior stay inside and darken.
    // Without the shift the cell is larger than the shell, and the crown's own density attenuated
    // the sunlit leaves along with the shaded ones.
    const float2 volume = volumeOcclusion(input.worldPos + crownNormal * occlusionTexel.w);
    float3 lighting = float3(0.0, 0.0, 0.0);
    if (directionalDirectionIntensity.w > 0.0) {
        const float3 lightDirection = normalize(directionalDirectionIntensity.xyz);
        const float3 sunRadiance = directionalColor.rgb * directionalDirectionIntensity.w;
        // The CSM shadow attenuates the entire sun contribution of the leaf (diffuse and highlight
        // exist only in light); sampled at the fragment point, the normal offset is the crown
        // normal.
        const float sunLit = sunVisibility(input.worldPos, crownNormal, volume.y);
        lighting += shadeLight(
            leafNormal, viewDirection, lightDirection, sunRadiance, albedo, metallic, roughness, f0
        ) * sunLit;
        // Leaf translucency: the sun is behind the leaf relative to the camera — a thin blade
        // transmits light (the translucency share, a warm hue shift), a forward lobe by the
        // camera–sun angle. Attenuated by the same shadow and transmittance as the diffuse: in
        // the interior of the crown there is no translucency, the silhouette against the sun and
        // thin spots glow.
        [branch]
        if (translucency > 0.001) {
            const float backLit = saturate(-dot(shadingBasis, lightDirection));
            const float lobe = pow(saturate(dot(viewDirection, -lightDirection)), 3.0);
            lighting += sunRadiance * albedo * float3(1.0, 0.95, 0.6) *
                        (translucency * backLit * (0.3 + 0.7 * lobe) * sunLit);
        }
    }
    const int pointLightCount = (int)sceneLightCounts.x;
    [loop]
    for (int i = 0; i < pointLightCount; ++i) {
        const float3 toLight = pointLightPositionsRange[i].xyz - input.worldPos;
        const float distanceToLight = length(toLight);
        const float radius = pointLightPositionsRange[i].w;
        if (radius <= 0.0 || distanceToLight >= radius) {
            continue;
        }
        const float normalizedDistance = saturate(distanceToLight / radius);
        float attenuation = 1.0 - normalizedDistance * normalizedDistance;
        attenuation *= attenuation;
        lighting += shadeLight(
            leafNormal, viewDirection, normalize(toLight),
            pointLightColorsIntensity[i].rgb * pointLightColorsIntensity[i].w * attenuation,
            albedo, metallic, roughness, f0
        );
    }

    const float3 ambient = ambientColorIntensity.rgb * ambientColorIntensity.w * albedo *
                           lerp(1.0, volume.x, occlusionParams.x);
    const float3 color = (ambient + lighting) * input.fragLight;

    PSOutput output;
    output.color = float4(color, 1.0);
    output.emissive = float4(0.0, 0.0, 0.0, 1.0);
    return output;
}
