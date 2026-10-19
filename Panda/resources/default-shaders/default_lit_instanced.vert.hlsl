#include "panda/vertex_ubo.hlsli"

struct VSInput {
    float3 position : POSITION;
    float2 texCoord : TEXCOORD0;
    float3 normal : NORMAL;
    float4 color : COLOR0;
    float light : TEXCOORD1;
    uint id : TEXCOORD2;
    // Instance data (the second vertex stream, per-instance step): the columns of the
    // instance transform matrix (glm column-major). The locations continue the vertex ones.
    float4 iModel0 : TEXCOORD3;
    float4 iModel1 : TEXCOORD4;
    float4 iModel2 : TEXCOORD5;
    float4 iModel3 : TEXCOORD6;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float2 fragTexCoord : TEXCOORD2;
    float4 fragColor : TEXCOORD3;
    float fragLight : TEXCOORD4;
    nointerpolation uint fragId : TEXCOORD5;
};

VSOutput main(VSInput input) {
    VSOutput output;
    // The rows of the assembled matrix = glm columns → this is the transposed instance matrix;
    // mul(row-vector, M^T) is equivalent to M * v.
    float4x4 instMtx = float4x4(input.iModel0, input.iModel1, input.iModel2, input.iModel3);
    float4 localPos = mul(float4(input.position, 1.0), instMtx);
    float3 localNormal = mul(input.normal, (float3x3) instMtx);
    float4 worldPos = mul(modelMtx, localPos);
    output.position = mul(projViewMtx, worldPos);
    output.worldPos = worldPos.xyz;
    output.worldNormal = normalize(mul((float3x3) modelMtx, localNormal));
    output.fragTexCoord = input.texCoord;
    output.fragColor = input.color;
    output.fragLight = input.light;
    output.fragId = input.id;
    return output;
}
