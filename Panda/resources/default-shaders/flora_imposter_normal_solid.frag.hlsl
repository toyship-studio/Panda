// Baking the NORMALS of the impostor atlas for NON-PBR materials (no alpha cutoff):
// world normals as is — opaque geometry needs no cutout.
struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float2 fragTexCoord : TEXCOORD2;
    float4 fragColor : TEXCOORD3;
    float fragLight : TEXCOORD4;
    nointerpolation uint fragId : TEXCOORD5;
    bool isFrontFace : SV_IsFrontFace;
};

float4 main(PSInput input) : SV_Target0 {
    float3 normal = normalize(input.worldNormal);
    if (!input.isFrontFace) { normal = -normal; }
    return float4(normal * 0.5 + 0.5, 1.0);
}
