#pragma once

#include "Panda/Assets/Base/AssetRef.hpp"
#include "Panda/Assets/TextureAsset.hpp"
#include "Panda/Base/Base.hpp"

#include <Miren/Base.hpp>

#include <array>
#include <cstdint>
#include <glm/glm.hpp>

namespace Panda {

// The assembled frame lighting of the world (the counterpart of WorldLightingSettings:
// settings → data), the CPU side of the scene. The shaders see it as the scene blocks of the
// shader features — PANDA_LIGHTS, PANDA_SHADOWS, PANDA_OCCLUSION, PANDA_ENVIRONMENT
// (ShaderInterface/ShaderBlocks.hpp, SceneBlocks): one block per feature, assembled from this
// struct once per frame; a feature the project has off is neither assembled nor bound.

static constexpr uint32_t RENDERER_MAX_POINT_LIGHTS = 8;

struct DirectionalLightData final {
    glm::vec3 direction = glm::vec3(0.f, 0.f, 1.f); // Toward the light (surface → light)
    float intensity = 0.f;
    Color color = Color(1.f, 1.f, 1.f, 1.f);
    bool castShadows = false; // the frame's sun casts cascaded shadows (does not go into the UBO)
};

struct PointLightData final {
    glm::vec3 position = glm::vec3(0.f);
    float radius = 10.f;
    Color color = Color(1.f, 1.f, 1.f, 1.f);
    float intensity = 1.f;
    glm::vec3 direction = glm::vec3(0.f, 0.f, -1.f); // where a spot shines (world)
    float coneAngle = 0.f;                            // the full angle of its cone, degrees; 0 — all around
    float coneSoftness = 0.25f;                       // the share of the cone that fades to its edge
    bool castShadows = false;                         // the lamp casts shadows (does not go into the UBO)
};

// Shadows of the lamps (LampShadowMap): the tiles lie in a row along U of one depth atlas
// (comparison sampler). A spot has one tile, a point light six (the faces of a cube: the axis a
// point lies along, + before -). tileCount 0 — no lamp casts shadows.
static constexpr uint32_t LAMP_SHADOW_MAX_TILES = 8;

struct LampShadowData final {
    AssetRef<TextureAsset> atlas; // DEPTH32F, COMPARE_LESS_EQUAL; LAMP_SHADOW_MAX_TILES × tileSize wide
    uint32_t tileCount = 0;
    uint32_t tileSize = 0;
    std::array<glm::mat4, LAMP_SHADOW_MAX_TILES> textureMatrices{}; // world → (u, v, depth)·w of the atlas
    // Per lamp of the frame (the order of pointLights): x — its first tile, y — the number of its
    // tiles (0 — no shadow), z — the size of a texel per meter of distance from the lamp, w — the
    // near plane of its projection.
    std::array<glm::vec4, RENDERER_MAX_POINT_LIGHTS> lamps{};
    float normalOffset = 0.f; // receiver offset along the normal, in texels
    float receiverBias = 0.f; // receiver depth offset, in texels
};


// Cascaded sun shadows (CSM): the cascades lie as tiles along U of a single depth atlas
// (comparison sampler). A cascade is a slice by distance from the camera (the shader takes the
// first one whose boundary is farther than the point; the slices are concentric — they do not
// swim when the camera rotates) and a "world → (u, v, depth) of the tile" matrix.
// cascadeCount 0 — no shadows.
static constexpr uint32_t RENDERER_MAX_SHADOW_CASCADES = 4;

struct ShadowData final {
    AssetRef<TextureAsset> atlas; // DEPTH32F, COMPARE_LESS_EQUAL; width = cascadeCount × tileSize
    uint32_t cascadeCount = 0;
    uint32_t tileSize = 0; // side of a cascade tile (texels)
    std::array<glm::mat4, RENDERER_MAX_SHADOW_CASCADES> textureMatrices{}; // world → tile (u, v, depth)
    std::array<float, RENDERER_MAX_SHADOW_CASCADES> splits{}; // cascade far boundary (m from the camera)
    std::array<float, RENDERER_MAX_SHADOW_CASCADES> texelWorld{}; // world size of a cascade texel (m)
    std::array<float, RENDERER_MAX_SHADOW_CASCADES> depthScale{}; // depth units per meter of cascade depth
    float distance = 0.f;                                         // shadow distance (edge fade)
    float strength = 0.f;                                         // strength (0 — no shadows)
    float normalOffset = 0.f; // receiver offset along the normal, in cascade texels
    float receiverBias = 0.f; // receiver depth offset, in cascade texels
    float slopeBias = 0.f;    // rasterizer slope bias of the depth pass
    float softness = 0.f;     // angular radius of the sun (degrees): the penumbra per meter of caster distance
};

// Occlusion Volume (context/lighting.md):
// a grid of chunks anchored to the world (a chunk = chunkCells² cells × slices slices), the
// resident window of windowChunks² chunks around the camera in a single RGBA8 3D texture (R —
// sky visibility, dims ambient; G and B — two sun transmittance buffers: a sun rebake is
// published into the free channel and blended in with the weight transmittanceBlend;
// transmittance within CSM — with a small weight, beyond it — with the full one). The slot of a
// chunk in the volume is the chunk index modulo windowChunks; a slot's tile is chunkCells + a
// one-cell border on each side (a seamless trilinear sample at the joint), the slices go along
// the y axis of the texture from the bottom of the chunk volume (originY, each chunk has its own).
// windowChunks 0 or an empty volume — no occlusion.
static constexpr uint32_t OCCLUSION_MAX_CHUNKS = 64; // window slots (windowChunks ≤ 8)

struct OcclusionChunk final {
    glm::ivec2 chunk = glm::ivec2(0); // index of the world chunk in the slot
    float originY = 0.f;              // bottom of the chunk volume (m)
    float weight = 0.f;               // fade-in 0..1 (0 — slot not baked, the shader reads "open")
};

struct OcclusionVolumeData final {
    AssetRef<TextureAsset> volume; // RGBA8 3D: (windowChunks·tileSize) × slices × (windowChunks·tileSize)
    float cellSize = 2.f;
    uint32_t chunkCells = 64;
    uint32_t tileSize = 66;    // chunkCells + 2 (border)
    uint32_t slices = 48;
    glm::ivec2 windowMin = glm::ivec2(0); // minimum chunk of the resident window
    uint32_t windowChunks = 0;            // window chunks per side (0 — off)
    float aoStrength = 0.f;               // strength on ambient (0 — off)
    float sunStrengthNear = 0.f;          // strength on the sun within the CSM distance
    float sunStrengthFar = 0.f;           // … beyond the CSM distance
    float surfaceOffset = 2.f;            // sample offset from the surface along the normal (m)
    float contrast = 1.f;                 // exponent of the sky visibility
    std::array<OcclusionChunk, OCCLUSION_MAX_CHUNKS> chunks{}; // by slots (z·windowChunks + x)
    glm::vec2 focus = glm::vec2(0.f);     // window focus (xz, m): falloff to open is computed from it
    float fadeRadius = 0.f;               // distance from the focus beyond which there is no occlusion (m)
    float transmittanceBlend = 0.f;       // weight of transmittance channel B vs G (sun rebake cross-fade)
};

// Environment map of the world (context/lighting.md): an equirect texture (the top of
// the image is the zenith, u = atan2(z, x)), the system input environmentMap. An empty map — the
// built-in sky gradient is in the slot (StaticResources::defaultSkyTexture), no PBR reflections.
struct EnvironmentData final {
    AssetRef<TextureAsset> map;
    float intensity = 1.f; // exposure of the map: sky and reflections together
    // The light of the sky baked for the lit shaders (EnvironmentLighting): the atlas of the
    // irradiance and the prefiltered radiance, the system input environmentLighting — of the map,
    // or of the built-in gradient when the world has no map. Empty — no image based light, only
    // the flat ambient (the feature is off, or nothing is baked yet).
    AssetRef<TextureAsset> lighting;
};

// Screen-space ambient occlusion (SsaoRenderer): computed from the depth of the frame that was
// drawn last and read by the frame being drawn, which finds its points in it with the
// view-projection of that frame. An invalid map — no occlusion.
struct SsaoData final {
    Miren::TextureHandle map = MIREN_INVALID_HANDLE; // r — the share of the sky a point sees, g — its view depth (m)
    glm::mat4 viewProj = glm::mat4(1.f);             // world → clip of the frame the map was computed from
    float intensity = 0.f;
};

struct WorldLightingData final {
    Color ambientColor = Color(1.f, 1.f, 1.f, 1.f);
    float ambientIntensity = 0.12f;
    DirectionalLightData directionalLight;
    std::array<PointLightData, RENDERER_MAX_POINT_LIGHTS> pointLights;
    uint32_t pointLightCount = 0;
    glm::vec3 cameraPosition = glm::vec3(0.f); // camera world position (view direction for PBR)
    ShadowData shadows; // sun shadows of this frame (assembled by World)
    LampShadowData lampShadows; // the shadows of the lamps of this frame
    OcclusionVolumeData occlusion;  // occlusion volume (assembled by World from flora scattering)
    EnvironmentData environment;    // environment map of the world (the world's lighting settings)
    SsaoData ssao;                  // the ambient occlusion the last frame left (World::setScreenOcclusion)
};

} // namespace Panda
