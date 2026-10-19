cbuffer UBO {
    float4x4 projViewMtx;
};

struct VSInput {
    float3 position : POSITION;
    float2 texCoord : TEXCOORD0;
    float4 color : COLOR0;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float4 fragColor : TEXCOORD0;
    float2 fragTexCoord : TEXCOORD1;
};

VSOutput main(VSInput input) {
    VSOutput output;
    output.position = mul(projViewMtx, float4(input.position, 1.0));
    output.fragTexCoord = input.texCoord;
    output.fragColor = input.color;
    return output;
}
