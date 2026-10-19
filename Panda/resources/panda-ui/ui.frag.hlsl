Texture2D u_texture;
SamplerState u_textureSampler;

// The PS input must mirror the VS output including SV_POSITION: DXBC links the
// stages by register numbers, omitting the position shifts all attributes.
struct PSInput {
    float4 position : SV_POSITION;
    float4 fragColor : TEXCOORD0;
    float2 fragTexCoord : TEXCOORD1;
};


// Scene MRT: the second output is the emissive buffer (the glow contribution; there is none here).
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
    outColor *= u_texture.Sample(u_textureSampler, input.fragTexCoord);
    if (outColor.a <= 0.01) {
        discard;
    }
    return makeOutput(outColor);
}
