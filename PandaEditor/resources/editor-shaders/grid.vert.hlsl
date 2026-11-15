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
    // The grid marks the plane y = 0 and lies a little under it: a floor modelled at y = 0 would
    // otherwise share its depth with the grid and flicker through it. The gap grows with the
    // height of the camera, as the precision of the depth buffer falls with the distance: two
    // thousandths of the height is far above that precision at any range of the grid and too
    // little to see.
    const float gap = 0.002 * max(abs(gCameraWorldPos.y), 0.05);
    float4 pos = float4(input.aPos.x * gridSize, -gap, input.aPos.y * gridSize, 1.0);
    pos.x += gCameraWorldPos.x;
    pos.z += gCameraWorldPos.z;
    output.position = mul(projViewMtx, pos);
    output.worldPos = pos.xyz;
    return output;
}
