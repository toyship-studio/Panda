// Cascade depth pass for impostor flora billboards: a quad driven by the mat4 instance, but
// turned TOWARDS THE LIGHT (not the camera) — the shadow gets a true tree silhouette from the
// sun's side, stable when the camera flies around. The atlas view is also from the sun's side.
// No crossfade: the PCF softness of the shadow hides the view change.
#include "panda/vertex_ubo.hlsli"

// The MATERIAL_FIELDS layout is the floraBillboardMaterial contract (bindFields binds the blob
// as a whole); cameraLocal is the SCENE camera: the mesh zone threshold mirrors the main billboard.
cbuffer MATERIAL_FIELDS {
    float4 sizeParams;  // x — mesh width (m), y — height, z — AABB minY, w — number of views
    float4 cameraLocal; // xyz — scene camera in terrain local space; w — near threshold (mesh zone)
    float4 fadeParams;  // not read by the depth pass
    uint doubleSided;   // the quad faces the light (culling off) — read by the render state
};

// Light direction — SCENE_LIGHTS (bound by bindShadowDepthProgram for the shadow variant).
#include "panda/scene_lights.hlsli"

struct VSInput {
    float2 corner : POSITION;    // x ∈ [-0.5..0.5], y ∈ [0..1]
    float2 texCoord : TEXCOORD0; // tile uv (v=0 — top of the atlas)
    float4 iModel0 : TEXCOORD1;  // columns of the instance mat4 (glm column-major)
    float4 iModel1 : TEXCOORD2;
    float4 iModel2 : TEXCOORD3;
    float4 iModel3 : TEXCOORD4;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float2 atlasUV : TEXCOORD0;
};

static const float TWO_PI = 6.28318530718;

VSOutput main(VSInput input) {
    VSOutput output;
    float4x4 instMtx = float4x4(input.iModel0, input.iModel1, input.iModel2, input.iModel3);
    float3 base = mul(float4(0.0, sizeParams.z, 0.0, 1.0), instMtx).xyz;
    float scale = length(input.iModel0.xyz);
    float yaw = atan2(-input.iModel0.z, input.iModel0.x);

    // The near zone is meshes: there the real geometry casts the shadow. The criterion is
    // bit-for-bit the same as in the main billboard (16 m block, near face ≤ Flora Distance).
    float2 blockCenter = (floor(base.xz / 16.0) + 0.5) * 16.0;
    float blockToCamera = distance(blockCenter, cameraLocal.xz);
    if (cameraLocal.w > 0.0 && blockToCamera - 16.0 * 0.71 <= cameraLocal.w) {
        output.position = float4(0.0, 0.0, 2.0, 1.0);
        output.atlasUV = float2(0.0, 0.0);
        return output;
    }

    // The "observer" is the sun: the quad is perpendicular to the light in the horizontal plane,
    // the atlas view is from the light's side. A vertical light degenerates the azimuth — a fixed
    // one is used.
    float2 toLight = directionalDirectionIntensity.xz;
    if (dot(toLight, toLight) < 1.0e-6) { toLight = float2(1.0, 0.0); }
    float2 forward = normalize(toLight);
    float viewAngle = atan2(forward.y, forward.x);
    float relative = viewAngle + yaw;
    float tiles = sizeParams.w;
    float tile = round(relative / TWO_PI * tiles);

    float2 right = float2(forward.y, -forward.x);
    float3 local = base;
    local.xz += right * (input.corner.x * sizeParams.x * scale);
    local.y += input.corner.y * sizeParams.y * scale;

    tile = tile - floor(tile / tiles) * tiles; // mod into [0..tiles)

    output.position = mul(projViewMtx, mul(modelMtx, float4(local, 1.0)));
    output.atlasUV = float2((tile + input.texCoord.x) / tiles, input.texCoord.y);
    return output;
}
