Texture2D colorTexture;
SamplerState colorTextureSampler;
Texture2D selectedGeometryTexture;
SamplerState selectedGeometryTextureSampler;

// The PS input mirrors the VS output, including SV_POSITION: DXBC links stages by registers.
struct PSInput {
    float4 position : SV_POSITION;
    float2 fragTexCoord : TEXCOORD0;
};

float4 getOutlineColor() {
    return float4(0.92, 0.55, 0.0, 1.0);
}

float4 main(PSInput input) : SV_Target0 {
    float4 color = colorTexture.Sample(colorTextureSampler, input.fragTexCoord);
    float4 selection = selectedGeometryTexture.Sample(selectedGeometryTextureSampler, input.fragTexCoord);
    return color + selection * getOutlineColor();
}
