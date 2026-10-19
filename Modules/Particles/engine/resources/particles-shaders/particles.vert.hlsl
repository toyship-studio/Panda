// Particle: a [-0.5..0.5]² quad from a mat4 instance (the columns are the camera-facing basis ×
// size and the world position; built by the system on the CPU) and the instance color. The
// instance stream contract is the same as in lit_instanced: the locations continue the vertex ones.
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
    float4 iColor : TEXCOORD7;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float4 fragColor : TEXCOORD0;
    float2 fragTexCoord : TEXCOORD1;
};

VSOutput main(VSInput input) {
    VSOutput output;
    float4x4 instMtx = float4x4(input.iModel0, input.iModel1, input.iModel2, input.iModel3);
    float4 localPos = mul(float4(input.position, 1.0), instMtx);
    float4 worldPos = mul(modelMtx, localPos);
    output.position = mul(projViewMtx, worldPos);
    output.fragColor = input.color * input.iColor;
    output.fragTexCoord = input.texCoord;
    return output;
}
