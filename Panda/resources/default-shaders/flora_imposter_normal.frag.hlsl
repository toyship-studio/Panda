// Baking the NORMALS of the impostor atlas (PBR materials): the mesh's world normals into color
// (encode ×0.5+0.5), the silhouette is a discard by the baseColor alpha (the same cutout as for
// the color). The MATERIAL_FIELDS layout must match default_pbr.frag — the fields are bound by
// the primitive's material as is.
cbuffer MATERIAL_FIELDS {
    float4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    float4 emissiveFactor;
    uint alphaMode;
    float alphaCutoff;
    uint doubleSided;
    float emissiveIntensity;
};

Texture2D baseColorTexture;
SamplerState baseColorTextureSampler;
Texture2D metallicRoughnessTexture;
SamplerState metallicRoughnessTextureSampler;
Texture2D normalTexture;
SamplerState normalTextureSampler;
Texture2D occlusionTexture;
SamplerState occlusionTextureSampler;
Texture2D emissiveTexture;
SamplerState emissiveTextureSampler;

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
    if (alphaMode == 1) {
        float alpha =
            baseColorTexture.Sample(baseColorTextureSampler, input.fragTexCoord).a *
            baseColorFactor.a;
        if (alpha < alphaCutoff) { discard; }
    }
    float3 normal = normalize(input.worldNormal);
    if (doubleSided != 0 && !input.isFrontFace) { normal = -normal; }
    return float4(normal * 0.5 + 0.5, 1.0);
}
