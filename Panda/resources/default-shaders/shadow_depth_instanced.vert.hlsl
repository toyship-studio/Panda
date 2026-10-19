// Depth-only shadow map pass (CSM) for instances (mat4 stream, like default_lit_instanced).
#include "panda/vertex_ubo.hlsli"

struct VSInput {
    float3 position : POSITION;
    float2 texCoord : TEXCOORD0;
    float3 normal : NORMAL;
    float4 color : COLOR0;
    float light : TEXCOORD1;
    uint id : TEXCOORD2;
    float4 iModel0 : TEXCOORD3;
    float4 iModel1 : TEXCOORD4;
    float4 iModel2 : TEXCOORD5;
    float4 iModel3 : TEXCOORD6;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float2 fragTexCoord : TEXCOORD0;
};

VSOutput main(VSInput input) {
    VSOutput output;
    float4x4 instMtx = float4x4(input.iModel0, input.iModel1, input.iModel2, input.iModel3);
    float4 localPos = mul(float4(input.position, 1.0), instMtx);
    float4 worldPos = mul(modelMtx, localPos);
    output.position = mul(projViewMtx, worldPos);
    output.position.z = max(output.position.z, 0.0); // pancaking, see shadow_depth.vert
    output.fragTexCoord = input.texCoord;
    return output;
}
