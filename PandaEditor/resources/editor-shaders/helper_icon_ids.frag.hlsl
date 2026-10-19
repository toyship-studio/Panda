Texture2D iconTexture;
SamplerState iconTextureSampler;

cbuffer HELPER_META {
    float4 iconTint;
    uint4 helperMeta;
};

// The PS input mirrors the VS output, including SV_POSITION: DXBC links stages by registers.
struct PSInput {
    float4 position : SV_POSITION;
    float2 fragUv : TEXCOORD0;
};

uint main(PSInput input) : SV_Target0 {
    float4 icon = iconTexture.Sample(iconTextureSampler, input.fragUv);
    if (icon.a <= 0.05) {
        discard;
    }
    return helperMeta.x;
}
