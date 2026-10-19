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


// Scene MRT: the second output is the emissive buffer (glow contribution; none here).
struct PSOutput {
    float4 color : SV_Target0;
    float4 emissive : SV_Target1;
};

PSOutput makeOutput(float4 color) {
    PSOutput output;
    output.color = color;
    output.emissive = float4(0.0, 0.0, 0.0, 0.0);
    return output;
}

PSOutput main(PSInput input) {
    float4 icon = iconTexture.Sample(iconTextureSampler, input.fragUv);
    if (icon.a <= 0.05) {
        discard;
    }
    // HDR: the scene buffer holds linear light; the icon and tint are sRGB.
    return makeOutput(float4(pow(abs(icon.rgb * iconTint.rgb), 2.2), icon.a * iconTint.a));
}
