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
    float2 maskPos : TEXCOORD2; // the position in UI points, for the rounded clip of the command
};

VSOutput main(VSInput input) {
    VSOutput output;
    output.position = mul(projViewMtx, float4(input.position, 1.0));
    output.fragTexCoord = input.texCoord;
    output.fragColor = input.color;
    output.maskPos = input.position.xy;
    return output;
}
