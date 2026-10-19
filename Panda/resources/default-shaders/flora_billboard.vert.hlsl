// Impostor plant billboard: a quad driven by the mat4 instance (the same stream as the mesh —
// the lit_instanced contract), rotated cylindrically towards the camera. The atlas view is
// selected by the "camera → plant" angle minus the instance yaw — the billboard shows the side
// of the mesh that an observer would see. At the border of the view sectors the content is
// CROSSFADED between two adjacent tiles (the weight goes to the frag): rotating around the tree
// does not make the image pop.
#include "panda/vertex_ubo.hlsli"

// Billboard parameters are runtime fields of the material (floraBillboardMaterial), fed by the
// owner per species before submit. The MATERIAL_FIELDS composition is the same as in the frag
// (one program buffer).
cbuffer MATERIAL_FIELDS {
    float4 sizeParams;  // x — mesh width (m), y — height, z — AABB minY, w — number of views
    float4 cameraLocal; // xyz — camera in terrain local space; w — near threshold (mesh zone)
    float4 fadeParams;  // x — fade-out start (m), y — end (read by the frag)
    uint doubleSided;   // the quad always faces the camera (culling off) — read by the render state
    float4 surfaceParams; // crown surface (metallic/roughness/translucency) — read by the frag
};

struct VSInput {
    float2 corner : POSITION;    // x ∈ [-0.5..0.5], y ∈ [0..1]
    float2 texCoord : TEXCOORD0; // tile uv (v=0 — top of the atlas)
    // Instance (second vertex stream): the columns of the transform matrix (glm column-major),
    // the locations continue the vertex ones.
    float4 iModel0 : TEXCOORD1;
    float4 iModel1 : TEXCOORD2;
    float4 iModel2 : TEXCOORD3;
    float4 iModel3 : TEXCOORD4;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float2 atlasUV0 : TEXCOORD1;              // nearest view
    float2 atlasUV1 : TEXCOORD2;              // neighbor in the viewing direction
    // x — yaw (rotation of the baked normal into the world), y — crossfade weight uv0→uv1
    // (0 — pure nearest view, 0.5 — sector border, 50/50).
    nointerpolation float2 yawBlend : TEXCOORD3;
    // Per-instance tone jitter (the crown/grass formula) — multiplies the frag's albedo.
    nointerpolation float3 tint : TEXCOORD4;
    // Crown volume for the frag: axis base (world), quad half-width and height (world, m) —
    // the frag builds the implied crown ellipsoid from them (self-shadowing, shadow point).
    nointerpolation float3 worldBase : TEXCOORD5;
    nointerpolation float2 quadSize : TEXCOORD6;
    float2 quadUV : TEXCOORD7; // x — across the quad [-1..1], y — height [0..1] from the base
};

static const float TWO_PI = 6.28318530718;
// Half-width of the crossfade band as a fraction of a sector: the transition takes the outer
// 25% of a sector on each side of the border (the central half is a pure, sharp view).
static const float BLEND_HALF = 0.25;

// Per-instance tone jitter: the FORMULA OF THE CROWN/GRASS VERTEX SHADERS (a hash of the same
// world XZ base position) — the billboard tone matches the mesh zone at the transition.
float3 instanceTint(float2 worldXZ) {
    // fmod keeps the sin argument small: fast-math sin lies at large coordinates
    // (the windNoise lesson — "keep the sin argument small").
    const float angle = fmod(dot(worldXZ, float2(12.9898, 78.233)), 6.2831853);
    const float h = frac(sin(angle) * 43758.5453);
    const float h2 = frac(h * 41.317 + 0.137);
    return (0.86 + 0.28 * h) * lerp(float3(1.08, 1.0, 0.86), float3(0.90, 1.0, 1.10), h2);
}

VSOutput main(VSInput input) {
    VSOutput output;
    float4x4 instMtx = float4x4(input.iModel0, input.iModel1, input.iModel2, input.iModel3);
    // Plant base on the ground: the mesh point (0, minY, 0) (pivot compensation in the matrix).
    float3 base = mul(float4(0.0, sizeParams.z, 0.0, 1.0), instMtx).xyz;
    float scale = length(input.iModel0.xyz); // uniform instance scale
    // rotY: X axis → (cos, 0, -sin) — the yaw is recovered from the first column.
    float yaw = atan2(-input.iModel0.z, input.iModel0.x);

    // Cylindrical billboard towards the camera (in terrain local space).
    float2 toCamera = cameraLocal.xz - base.xz;
    // The near zone is meshes. The criterion EXACTLY mirrors the mesh loop (a 16 m block is drawn
    // with meshes when its near face is ≤ Flora Distance): an instance in such a block
    // collapses to a point — no duplicates and no holes at the zone border.
    float2 blockCenter = (floor(base.xz / 16.0) + 0.5) * 16.0; // BLOCK_SIZE of the fine grid
    float blockToCamera = distance(blockCenter, cameraLocal.xz);
    // nearCutoff 0 = there is no mesh zone (Lab preview): without the guard the cutoff stayed
    // active within a half-block radius and killed the billboard in a sector of angles near the
    // camera.
    if (cameraLocal.w > 0.0 && blockToCamera - 16.0 * 0.71 <= cameraLocal.w) {
        output.position = float4(0.0, 0.0, 2.0, 1.0);
        output.worldPos = float3(0.0, 0.0, 0.0);
        output.atlasUV0 = float2(0.0, 0.0);
        output.atlasUV1 = float2(0.0, 0.0);
        output.yawBlend = float2(0.0, 0.0);
        output.tint = float3(1.0, 1.0, 1.0);
        output.worldBase = float3(0.0, 0.0, 0.0);
        output.quadSize = float2(1.0, 1.0);
        output.quadUV = float2(0.0, 0.0);
        return output;
    }
    // View: rotY(yaw) maps a point's azimuth as a → a−yaw, so an observer
    // at the world azimuth viewAngle sees the LOCAL view viewAngle + yaw
    // (the "−" sign turned the billboard by 2·yaw — up to 180° relative to the mesh zone).
    float viewAngle = atan2(toCamera.y, toCamera.x);
    float relative = viewAngle + yaw;
    float tiles = sizeParams.w;
    // Continuous "view number": the nearest tile is the sharp center of the sector, the neighbor
    // by the sign of the offset is mixed in within the band at the border (the weight is
    // continuous across the border: 0.5 on both sides — adjacent sectors join without a content
    // jump).
    float slot = relative / TWO_PI * tiles;
    float nearestTile = round(slot);
    float offset = slot - nearestTile; // [-0.5..0.5), 0 — exact view
    float blend = saturate((abs(offset) - (0.5 - BLEND_HALF)) / BLEND_HALF) * 0.5;
    float neighborTile = nearestTile + (offset >= 0.0 ? 1.0 : -1.0);

    // The quad plane faces the camera head-on: the content between views is interpolated by the
    // crossfade, so the image tweaking on a frontal quad no longer diverges from the mesh zone
    // (the former snap-plane to the view angle popped both the plane and the image at every
    // sector border).
    float2 forward = normalize(toCamera);
    float2 right = float2(forward.y, -forward.x);
    float3 local = base;
    local.xz += right * (input.corner.x * sizeParams.x * scale);
    local.y += input.corner.y * sizeParams.y * scale;

    nearestTile = nearestTile - floor(nearestTile / tiles) * tiles;   // mod into [0..tiles)
    neighborTile = neighborTile - floor(neighborTile / tiles) * tiles;

    float4 worldPos = mul(modelMtx, float4(local, 1.0));
    output.position = mul(projViewMtx, worldPos);
    output.worldPos = worldPos.xyz;
    output.atlasUV0 = float2((nearestTile + input.texCoord.x) / tiles, input.texCoord.y);
    output.atlasUV1 = float2((neighborTile + input.texCoord.x) / tiles, input.texCoord.y);
    output.yawBlend = float2(yaw, blend);
    // Jitter only in the world (nearCutoff > 0 = a mesh zone exists): the Lab preview
    // (nearCutoff 0) compares the mesh and the impostor exactly, with no tone shift.
    const float3 worldBase = mul(modelMtx, float4(base, 1.0)).xyz;
    output.tint = cameraLocal.w > 0.0 ? instanceTint(worldBase.xz) : float3(1.0, 1.0, 1.0);
    output.worldBase = worldBase;
    output.quadSize = float2(
        length(mul((float3x3) modelMtx, float3(right.x, 0.0, right.y) * (0.5 * sizeParams.x * scale))),
        length(mul((float3x3) modelMtx, float3(0.0, sizeParams.y * scale, 0.0)))
    );
    output.quadUV = float2(input.corner.x * 2.0, input.corner.y);
    return output;
}
