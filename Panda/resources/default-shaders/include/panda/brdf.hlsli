// Cook-Torrance GGX (default_pbr, the veg_crown crown, the impostor billboard). shadeLight is
// the contribution of a single light source, already multiplied by N·L. diffuseAlbedo is what
// the source sees as diffuse: PBR passes albedo / PI (energy-conserving Lambert), foliage and
// billboards pass the raw albedo, the world's energy convention (default_lit, terrain, grass —
// raw N·L): with 1/π the crown under the same sun was three times darker than the scene, and
// the foliage color swatch did not match the lit leaf.
static const float PI = 3.14159265359;

float distributionGGX(float3 normal, float3 halfVector, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = saturate(dot(normal, halfVector));
    float denom = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 0.0001);
}

float geometrySchlickGGX(float nDotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotV / (nDotV * (1.0 - k) + k);
}

float geometrySmith(float nDotV, float nDotL, float roughness) {
    return geometrySchlickGGX(nDotV, roughness) * geometrySchlickGGX(nDotL, roughness);
}

float3 fresnelSchlick(float cosTheta, float3 f0) {
    return f0 + (1.0 - f0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 shadeLight(
    float3 normal, float3 viewDirection, float3 lightDirection, float3 radiance,
    float3 diffuseAlbedo, float metallic, float roughness, float3 f0
) {
    float nDotL = saturate(dot(normal, lightDirection));
    if (nDotL <= 0.0) {
        return float3(0.0, 0.0, 0.0);
    }
    float3 halfVector = normalize(viewDirection + lightDirection);
    float nDotV = saturate(dot(normal, viewDirection));
    float ndf = distributionGGX(normal, halfVector, roughness);
    float geometry = geometrySmith(nDotV, nDotL, roughness);
    float3 fresnel = fresnelSchlick(saturate(dot(halfVector, viewDirection)), f0);
    float3 specular = (ndf * geometry * fresnel) / max(4.0 * nDotV * nDotL, 0.0001);
    float3 diffuseFactor = (1.0 - fresnel) * (1.0 - metallic);
    return (diffuseFactor * diffuseAlbedo + specular) * radiance * nDotL;
}
