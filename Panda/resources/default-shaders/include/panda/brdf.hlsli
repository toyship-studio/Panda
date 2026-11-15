// The surface model of the lit shaders: Cook-Torrance with the GGX distribution, the
// height-correlated Smith visibility and the Schlick Fresnel (default_pbr, the veg_crown crown,
// the impostor billboard).
//   shadeLight   — the light of one source reflected by the surface, already multiplied by N.L.
//                  The intensity of a light in the engine is what a white matte surface facing
//                  it sends back (default_lit, terrain, grass: albedo * intensity * N.L). The
//                  physical model wants the light that arrives, PI times that: PBR passes
//                  radiance = color * intensity * PI and diffuseAlbedo = albedo / PI. Foliage and
//                  billboards pass the intensity and the raw albedo: the same diffuse, a weaker
//                  highlight, as they were tuned.
//   shadeAmbient — the light of the surroundings (the flat ambient, the environment map)
//                  reflected by the surface: diffuse and specular, with the energy of multiple
//                  scattering kept, so a rough metal does not go dark.
//   filterRoughness — the roughness a pixel needs so that a highlight on a curved or
//                  normal-mapped surface does not sparkle.
static const float PI = 3.14159265359;

float distributionGGX(float3 normal, float3 halfVector, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = saturate(dot(normal, halfVector));
    float denom = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 0.0001);
}

// The Smith visibility of GGX with the correlation of the masking and the shadowing heights:
// G / (4 N.L N.V) in one term.
float visibilitySmithGGX(float nDotV, float nDotL, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float lambdaV = nDotL * sqrt(nDotV * nDotV * (1.0 - a2) + a2);
    float lambdaL = nDotV * sqrt(nDotL * nDotL * (1.0 - a2) + a2);
    return 0.5 / max(lambdaV + lambdaL, 1.0e-5);
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
    float nDotV = max(dot(normal, viewDirection), 1.0e-4);
    float ndf = distributionGGX(normal, halfVector, roughness);
    float visibility = visibilitySmithGGX(nDotV, nDotL, roughness);
    float3 fresnel = fresnelSchlick(saturate(dot(halfVector, viewDirection)), f0);
    float3 specular = ndf * visibility * fresnel;
    float3 diffuseFactor = (1.0 - fresnel) * (1.0 - metallic);
    return (diffuseFactor * diffuseAlbedo + specular) * radiance * nDotL;
}

// The reflectance of a surface lit by its whole surroundings, split from the light itself (the
// split-sum approximation): reflected = radiance * (f0 * x + y). An analytic fit of the
// integral (Karis, "Physically Based Shading on Mobile") — no lookup texture.
float2 environmentBrdf(float nDotV, float roughness) {
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    const float4 r = roughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}

// How much of the reflected surroundings an occlusion of the diffuse light hides: a mirror looks
// along one ray and is hidden less than the hemisphere, a rough surface as much (Lagarde).
float specularOcclusion(float nDotV, float occlusion, float roughness) {
    return saturate(pow(nDotV + occlusion, exp2(-16.0 * roughness - 1.0)) - 1.0 + occlusion);
}

// The light of the surroundings reflected by a surface. `irradiance` — the diffuse light that
// arrives (over PI: the flat ambient, environmentDiffuse), `radiance` — the light that arrives
// along the reflection for this roughness (environmentSpecular), `occlusion` — the share of the
// surroundings the surface sees (1 — open). The specular part keeps the energy the single
// scattering model loses on rough surfaces (Fdez-Aguera, "A Multiple-Scattering Microfacet Model
// for Real-Time Image-based Lighting").
// The occlusion of a surface of this albedo with the light that bounces between it and what
// occludes it: a bright crease is lighter than the bare visibility says, a dark one is not
// (Jimenez et al., "Practical Real-Time Strategies for Accurate Indirect Occlusion", the fit of
// the multiple bounces).
float3 occlusionWithBounces(float occlusion, float3 albedo) {
    const float3 a = 2.0404 * albedo - 0.3324;
    const float3 b = -4.7951 * albedo + 0.6417;
    const float3 c = 2.7552 * albedo + 0.6903;
    return max(float3(occlusion, occlusion, occlusion), ((occlusion * a + b) * occlusion + c) * occlusion);
}

float3 shadeAmbient(
    float3 normal, float3 viewDirection, float3 irradiance, float3 radiance,
    float3 albedo, float metallic, float roughness, float3 f0, float occlusion
) {
    const float nDotV = max(dot(normal, viewDirection), 1.0e-4);
    const float2 scaleBias = environmentBrdf(nDotV, roughness);
    const float3 grazing = max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), f0) - f0;
    const float3 fresnel = f0 + grazing * pow(1.0 - nDotV, 5.0);
    const float3 single = fresnel * scaleBias.x + scaleBias.y;
    const float lost = 1.0 - (scaleBias.x + scaleBias.y);
    const float3 average = f0 + (1.0 - f0) / 21.0;
    const float3 multiple = lost * single * average / (1.0 - average * lost);
    const float3 diffuseAlbedo = albedo * (1.0 - metallic);
    const float3 diffuse = diffuseAlbedo * (1.0 - single - multiple);
    return single * radiance * specularOcclusion(nDotV, occlusion, roughness) +
           (multiple * occlusion + diffuse * occlusionWithBounces(occlusion, diffuseAlbedo)) * irradiance;
}

// The roughness of a pixel whose normal turns across it: a curved or normal-mapped surface
// under a small bright light would sparkle with the roughness of the material (Tokuyoshi and
// Kaplanyan, "Improved Geometric Specular Antialiasing").
float filterRoughness(float3 normal, float roughness) {
    const float3 du = ddx(normal);
    const float3 dv = ddy(normal);
    const float variance = 0.25 * (dot(du, du) + dot(dv, dv));
    const float alpha = roughness * roughness;
    return sqrt(sqrt(saturate(alpha * alpha + min(2.0 * variance, 0.18))));
}
