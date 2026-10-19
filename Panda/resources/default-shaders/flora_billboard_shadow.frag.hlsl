// Cascade depth pass for impostor flora billboards: the silhouette is the color atlas alpha
// (premultiplied: α = coverage), below the threshold — a hole in the shadow. The MATERIAL_FIELDS
// layout is the floraBillboardMaterial contract (bindFields binds the blob as a whole).
cbuffer MATERIAL_FIELDS {
    float4 sizeParams;
    float4 cameraLocal;
    float4 fadeParams;
    uint doubleSided;
};

Texture2D imposterColorTexture;
SamplerState imposterColorTextureSampler;

struct PSInput {
    float4 position : SV_POSITION;
    float2 atlasUV : TEXCOORD0;
};

void main(PSInput input) {
    float alpha = imposterColorTexture.Sample(imposterColorTextureSampler, input.atlasUV).a;
    // Mip compensation (Castano, as in shadow_depth_masked): on the coarse texels of a cascade
    // the sample falls into the deep mips of the atlas, the average crown alpha drops below the
    // threshold — without the boost the billboard shadow would thin out as Shadow Distance grows.
    float2 texSize;
    imposterColorTexture.GetDimensions(texSize.x, texSize.y);
    const float2 uvTexels = input.atlasUV * texSize;
    const float2 dx = ddx(uvTexels);
    const float2 dy = ddy(uvTexels);
    const float mip = 0.5 * log2(max(max(dot(dx, dx), dot(dy, dy)), 1.0));
    alpha *= 1.0 + mip * 0.25;
    // The threshold is below the classic 0.5 — as in the main billboard: the atlas blurs thin
    // foliage into semi-transparent pixels, a 0.5 threshold would leave only the trunk's shadow.
    if (alpha < 0.3) {
        discard;
    }
}
