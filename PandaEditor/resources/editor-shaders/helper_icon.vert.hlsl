cbuffer HELPER_VERT {
    float4x4 viewProjMtx;
    float4 iconCenterAndSize;
    float4 viewportSize;
};

struct VSInput {
    float2 inPosition : POSITION;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float2 fragUv : TEXCOORD0;
};

VSOutput main(VSInput input) {
    VSOutput output;
    float4 clipCenter = mul(viewProjMtx, float4(iconCenterAndSize.xyz, 1.0));
    float2 ndcOffset = input.inPosition * (iconCenterAndSize.w * 2.0 / viewportSize.xy);
    output.position = clipCenter + float4(ndcOffset * clipCenter.w, 0.0, 0.0);
    output.fragUv = float2(input.inPosition.x * 0.5 + 0.5, 1.0 - (input.inPosition.y * 0.5 + 0.5));
    return output;
}
