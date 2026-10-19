// Depth-only shadow map pass with an alpha-mask: foliage/grass punch holes in the shadow too.
// The renderer reads the threshold and the alpha multiplier from the caster material's fields on
// the CPU (alphaCutoff, baseColorFactor.a — the lit template's name convention) and passes them
// via its own UBO: the material's MATERIAL_FIELDS layout is not needed here, the shader reads
// only baseColorTexture.
cbuffer SHADOW_MASK {
    float4 shadowMask; // x — alphaCutoff, y — alpha multiplier (baseColorFactor.a)
};

Texture2D baseColorTexture;
SamplerState baseColorTextureSampler;

struct PSInput {
    float4 position : SV_POSITION;
    float2 fragTexCoord : TEXCOORD0;
};

void main(PSInput input) {
    float alpha = baseColorTexture.Sample(baseColorTextureSampler, input.fragTexCoord).a * shadowMask.y;
    // Mip compensation (Castano, as in veg_crown.frag): a binary foliage mask has an average
    // coverage below the cutoff — on the coarse texels of a cascade (large Shadow Distance) the
    // sample falls into the deep mips, and without the boost ALL the foliage was discarded — the
    // crown shadow vanished, leaving a bare trunk. The mip is computed from the UV derivatives
    // manually.
    float2 texSize;
    baseColorTexture.GetDimensions(texSize.x, texSize.y);
    const float2 uvTexels = input.fragTexCoord * texSize;
    const float2 dx = ddx(uvTexels);
    const float2 dy = ddy(uvTexels);
    const float mip = 0.5 * log2(max(max(dot(dx, dx), dot(dy, dy)), 1.0));
    alpha *= 1.0 + mip * 0.25;
    if (alpha < shadowMask.x) {
        discard;
    }
}
