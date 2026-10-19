Texture2D baseColorTexture;
SamplerState baseColorTextureSampler;

// The PS input mirrors the VS output including SV_POSITION: DXBC links the stages by registers.
struct PSInput {
    float4 position : SV_POSITION;
    float4 fragColor : TEXCOORD0;
    float2 fragTexCoord : TEXCOORD1;
    nointerpolation uint fragId : TEXCOORD2;
};


// Scene MRT: second output — the emissive buffer (the glow contribution; there is none here).
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
    float4 outColor = input.fragColor;
    outColor *= baseColorTexture.Sample(baseColorTextureSampler, input.fragTexCoord);
    if (outColor.a <= 0.01) {
        discard;
    }
    // Linear pipeline: the sRGB texture is decoded on sampling, no conversion is needed.
    return makeOutput(outColor);
}
