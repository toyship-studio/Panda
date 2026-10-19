// Particle: instance color × texture, alpha-blend (the render state reads alphaMode 2), unlit.
cbuffer MATERIAL_FIELDS {
    uint alphaMode;
    uint doubleSided;
};

Texture2D baseColorTexture;
SamplerState baseColorTextureSampler;

struct PSInput {
    float4 position : SV_POSITION;
    float4 fragColor : TEXCOORD0;
    float2 fragTexCoord : TEXCOORD1;
};

// Scene MRT: the second output is the emissive buffer (particles do not glow in bloom).
struct PSOutput {
    float4 color : SV_Target0;
    float4 emissive : SV_Target1;
};

PSOutput main(PSInput input) {
    float4 color = input.fragColor * baseColorTexture.Sample(baseColorTextureSampler, input.fragTexCoord);
    if (color.a <= 0.005) {
        discard;
    }
    PSOutput output;
    output.color = color;
    output.emissive = float4(0.0, 0.0, 0.0, 0.0);
    return output;
}
