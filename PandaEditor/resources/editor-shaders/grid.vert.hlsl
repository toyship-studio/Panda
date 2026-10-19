cbuffer UBO1 {
    float4 gCameraWorldPos;
    float4x4 projViewMtx;
};

static const float gridSize = 100.0;

struct VSInput {
    float2 aPos : POSITION;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
};

VSOutput main(VSInput input) {
    VSOutput output;
    float4 pos = float4(input.aPos.x * gridSize, 0.0, input.aPos.y * gridSize, 1.0);
    pos.x += gCameraWorldPos.x;
    pos.z += gCameraWorldPos.z;
    output.position = mul(projViewMtx, pos);
    output.worldPos = pos.xyz;
    return output;
}
