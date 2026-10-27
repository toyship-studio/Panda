cbuffer UBO {
    float4x4 projViewMtx;
};

struct VSInput {
    float3 position : POSITION;
    float2 localPos : TEXCOORD0; // local position relative to the shape center (px)
    float4 color    : COLOR0;
    float4 params   : TEXCOORD1; // xy = half-size (px), z = radius (px), w = outline width (0 = fill)
};

struct VSOutput {
    float4 position : SV_POSITION;
    float4 color    : TEXCOORD0;
    float2 localPos : TEXCOORD1;
    float4 params   : TEXCOORD2;
    float2 maskPos  : TEXCOORD3; // the position in UI points, for the rounded clip of the command
};

VSOutput main(VSInput input) {
    VSOutput output;
    output.position = mul(projViewMtx, float4(input.position, 1.0));
    output.color = input.color;
    output.localPos = input.localPos;
    output.params = input.params;
    output.maskPos = input.position.xy;
    return output;
}
