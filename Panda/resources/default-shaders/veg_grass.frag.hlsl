// Vegetation grass: a tuft of intersecting billboards, cutout of the user texture × a MATERIAL
// root→tip gradient by height fraction (wind weight COLOR_0.a: 0 root → 1 tip — which is also the
// height by construction). The gradient is in the material, not in the vertices: the tool and the
// editor turn the SAME controls (a dark root gives the tuft volume without baking); the vertex
// COLOR_0.rgb is not read by the shader — it stays baked into the glTF for external viewers.
// View fade — dissolving of cards that are edge-on to the camera (the veg_crown pattern: the
// actual triangle normal from the worldPos derivatives, the dither pinned to UV, a guaranteed
// discard right at the edge).
// MATERIAL_FIELDS layout = the pbr set + the grass fields AT THE END (std140 order: the prefix
// matches PBR — template merge and the standard passes are compatible).

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
    // Dissolving of cards that are edge-on to the camera (0 — off). The field name is the shared
    // muting contract (the impostor bake zeroes viewFade by name).
    float viewFade;
    // Wind (vertex shader): strength and gustiness are SYSTEM fields (the engine sets the fields
    // of the same name in all materials — the scene wind, SceneWind); direction and phases come
    // from PANDA_FIELDS. The three-layer model is shared with the foliage and the terrain grass.
    float windStrength;
    float windGustiness;
    // Tuft gradient root→tip (dark root = volume): by the height fraction COLOR_0.a.
    float4 rootColor; // @color
    float4 tipColor; // @color
    // Per-tuft dissolve towards the far boundary of the flora layer (x — start, y — end, m from the
    // camera). The template default is "do not fade out"; the terrain sets it as a runtime field.
    float4 grassFade;
};

#define PANDA_SHADOW_SINGLE_TAP // vegetation: a single compare tap (see shadow.hlsli)
#include "panda/sun.hlsli"

Texture2D baseColorTexture;
SamplerState baseColorTextureSampler;

struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float2 fragTexCoord : TEXCOORD2;
    float4 fragColor : TEXCOORD3;
    float fragLight : TEXCOORD4;
    nointerpolation uint fragId : TEXCOORD5;
    float3 rootPos : TEXCOORD6; // tuft root in world space — the CSM shadow is sampled here
    bool isFrontFace : SV_IsFrontFace;
};

struct PSOutput {
    float4 color : SV_Target0;
    float4 emissive : SV_Target1;
};

PSOutput main(PSInput input) {
    const float4 tex = baseColorTexture.Sample(baseColorTextureSampler, input.fragTexCoord);
    if (tex.a < alphaCutoff) {
        discard;
    }

    const float3 viewDirection = normalize(cameraPosition.xyz - input.worldPos);
    // View fade of edge-on cards (the vertex normal is "up" and does not describe the card
    // plane: the actual triangle normal comes from the worldPos derivatives).
    [branch]
    if (viewFade > 0.001) {
        const float3 faceNormal = normalize(cross(ddx(input.worldPos), ddy(input.worldPos)));
        const float facing = abs(dot(faceNormal, viewDirection));
        if (facing < viewFade * 0.25) {
            discard;
        }
        const float fade = smoothstep(viewFade * 0.25, viewFade, facing);
        // Dither over the texture texels at the current mip: a cell ≈ a screen pixel, the pattern
        // is pinned to the tuft (as in veg_crown).
        float2 texSize;
        baseColorTexture.GetDimensions(texSize.x, texSize.y);
        const float2 dx = ddx(input.fragTexCoord) * texSize;
        const float2 dy = ddy(input.fragTexCoord) * texSize;
        const float mip = 0.5 * log2(max(max(dot(dx, dx), dot(dy, dy)), 1.0));
        const float2 ditherCell = floor(input.fragTexCoord * max(texSize * exp2(-floor(mip)), 8.0));
        const float ditherNoise =
            frac(52.9829189 * frac(dot(ditherCell, float2(0.06711056, 0.00583715))));
        if (fade < ditherNoise) {
            discard;
        }
    }

    // The card normal does NOT take part in lighting: the billboards of a tuft are turned in all
    // directions, and a per-card dot(N, L) tinted them in different shades — the eye picked out
    // separate planes. Grass is lit as a FIELD — with the world vertical as the normal (terrain
    // grass with its almost vertical normal reads the same way): the turn of a card towards the
    // sun does not affect the shade, only the height of the light above the horizon remains.
    const float3 fieldNormal = float3(0.0, 1.0, 0.0);
    // The gradient is a material one (fragColor.a = height fraction); fragColor.rgb is the
    // per-instance tint channel (field jitter, white for a single tuft); the vertex rgb takes no
    // part: it is baked by the export for external glTF viewers.
    const float3 gradient = lerp(rootColor.rgb, tipColor.rgb, input.fragColor.a);
    const float3 albedo = baseColorFactor.rgb * gradient * tex.rgb * input.fragColor.rgb;

    float3 lighting = float3(0.0, 0.0, 0.0);
    // Occlusion volume: a tuft under the canopy darkens (sampled at the root).
    const float2 volume = volumeOcclusion(input.rootPos);
    if (directionalDirectionIntensity.w > 0.0) {
        // Soft half-filled diffuse: grass without harsh light and shade, the shadowed side does
        // not go black. The CSM shadow is taken at the tuft ROOT (a card swaying in the wind would
        // pass through shadow edges and flicker); the normal offset points up, as for the terrain
        // grass (the card normal faces sideways, the acne comes from the ground under the tuft).
        const float shade =
            0.25 + 0.75 * saturate(dot(fieldNormal, normalize(directionalDirectionIntensity.xyz)));
        // The sample is taken 0.2 m ABOVE the root: the root lies on the terrain caster, the
        // depths coincide at the edge of an atlas texel — requantization on camera movement would
        // blink the whole tuft (acne). The lift takes the point out of the depth fight.
        lighting += directionalColor.rgb * directionalDirectionIntensity.w * albedo * shade *
                    sunVisibility(input.rootPos + float3(0.0, 0.2, 0.0), fieldNormal, volume.y);
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
        const float shade = 0.25 + 0.75 * saturate(dot(fieldNormal, toLight / distanceToLight));
        lighting += pointLightColorsIntensity[i].rgb * pointLightColorsIntensity[i].w *
                    attenuation * albedo * shade;
    }

    const float3 ambient = ambientColorIntensity.rgb * ambientColorIntensity.w * albedo *
                           lerp(1.0, volume.x, occlusionParams.x);
    const float3 color = (ambient + lighting) * input.fragLight;

    PSOutput output;
    output.color = float4(color, 1.0);
    output.emissive = float4(0.0, 0.0, 0.0, 1.0);
    return output;
}
