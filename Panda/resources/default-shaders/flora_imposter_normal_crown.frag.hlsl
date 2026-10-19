// Baking the NORMALS of the impostor atlas for the veg_crown crown: the normal of EACH leaf is the
// same one the mesh crown is lit with (a COPY of the normal branch of veg_crown.frag, keep in
// sync): the shoot normal tile through the tangent basis of the card plane, the plane is
// turned towards the camera of the view. The regular normal pass (flora_imposter_normal) writes
// the vertex normal — the crown ellipsoid: a distant billboard came out as a smooth ball next to
// the mottled mesh foliage. The silhouette is the same cutout as in the color pass (Castano
// mip compensation): the normals lie exactly under the color. The wind is stubbed out by the
// bake — the cards are static and the flutter is not baked in (as for the color).
// The MATERIAL_FIELDS layout = veg_crown (bindFields binds the blob as a whole); the vertex
// shader is veg_crown.vert (PSInput mirrors its VSOutput).

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
    float viewFade;
    float windStrength;
    float windGustiness;
    float canopyNormal;  // leaf lighting basis: 0 — card plane, 1 — crown volume normal
    float translucency;  // leaf translucency: share of sun through the blade when the sun is behind the leaf
};

// Bake view: an ortho camera — the direction TO THE OBSERVER is the same for the whole tile. The
// scene formula "camera − point" would be off by up to 26° at the frame edges and flip the plane
// of edge-on cards — their leaves would be printed facing away from the viewer.
cbuffer IMPOSTER_BAKE {
    float4 bakeToViewer; // xyz — unit direction from the mesh to the view's camera
};

Texture2D baseColorTexture;
SamplerState baseColorTextureSampler;
Texture2D normalTexture;
SamplerState normalTextureSampler;

struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float2 fragTexCoord : TEXCOORD2;
    float fragLight : TEXCOORD4;
    nointerpolation float3 fragTint : TEXCOORD6;
    float fragGust : TEXCOORD7;
};

// Tangent basis of the card from derivatives (cotangent frame, Schüler) — as in veg_crown.frag.
float3x3 cotangentFrame(float3 normal, float3 dp1, float3 dp2, float2 duv1, float2 duv2) {
    const float3 dp2perp = cross(dp2, normal);
    const float3 dp1perp = cross(normal, dp1);
    const float3 tangent = dp2perp * duv1.x + dp1perp * duv2.x;
    const float3 bitangent = dp2perp * duv1.y + dp1perp * duv2.y;
    const float invMax = rsqrt(max(dot(tangent, tangent), dot(bitangent, bitangent)) + 1.0e-12);
    return float3x3(tangent * invMax, bitangent * invMax, normal);
}

float4 main(PSInput input) : SV_Target0 {
    const float3 positionDx = ddx(input.worldPos);
    const float3 positionDy = ddy(input.worldPos);
    const float2 uvDx = ddx(input.fragTexCoord);
    const float2 uvDy = ddy(input.fragTexCoord);
    const float4 tile = baseColorTexture.Sample(baseColorTextureSampler, input.fragTexCoord);
    float2 tileTexSize;
    baseColorTexture.GetDimensions(tileTexSize.x, tileTexSize.y);
    const float2 dx = uvDx * tileTexSize;
    const float2 dy = uvDy * tileTexSize;
    const float mip = 0.5 * log2(max(max(dot(dx, dx), dot(dy, dy)), 1.0));
    if (tile.a * (1.0 + mip * 0.25) < alphaCutoff) {
        discard;
    }

    // The card plane — from derivatives, towards the observer of the view; the crown volume normal
    // is the fallback for a degenerate plane.
    const float3 crownNormal = normalize(input.worldNormal);
    const float3 faceCross = cross(positionDx, positionDy);
    const float faceLength2 = dot(faceCross, faceCross);
    float3 faceNormal = faceLength2 > 1.0e-20 ? faceCross * rsqrt(faceLength2) : crownNormal;
    if (dot(faceNormal, bakeToViewer.xyz) < 0.0) { faceNormal = -faceNormal; }

    const float4 normalSample = normalTexture.SampleLevel(
        normalTextureSampler, input.fragTexCoord, min(mip, 2.0)
    );
    float3 tileNormal = normalSample.rgb * 2.0 - 1.0;
    tileNormal.xy *= normalScale;
    tileNormal.y = -tileNormal.y;
    // Basis — as in veg_crown: card plane ↔ crown volume normal according to canopyNormal.
    const float3 mixedBasis = lerp(faceNormal, crownNormal, saturate(canopyNormal));
    const float3 shadingBasis = dot(mixedBasis, mixedBasis) > 1.0e-6 ? normalize(mixedBasis) : faceNormal;
    const float3x3 cardFrame = cotangentFrame(shadingBasis, positionDx, positionDy, uvDx, uvDy);
    const float3 leafNormal = normalize(mul(tileNormal, cardFrame));
    // Encode ×0.5+0.5; α = 1 is coverage (the billboard divides the filtered normal by α).
    return float4(leafNormal * 0.5 + 0.5, 1.0);
}
